#include "dev_bridge.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "network_manager.h"
#include "nvs.h"
#include "settings_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The dev bridge, board side (docs/DEV_BRIDGE_PLAN.md, phase 0). Tools on the PC (Claude Code hooks, scripts) post short messages to the
// PC server; this task asks the server for new ones with a long poll and hands them to the screen and the voice. Every request is signed
// with the pairing token (HMAC-SHA256 of "<time>:<method>:<path>"), which the board receives over the USB cable and keeps in its memory.

static const char *TAG = "DEV_BRIDGE";

#define QUEUE_SIZE 6
#define BODY_BYTES 14000

typedef struct {
    char text[300];
    char full[1000];  // the whole explanation, shown on a scrollable sheet
    int level;
    bool speak;
    int ask;  // the number of an approval to offer on the screen, 0 for a plain message
    int nopts;  // choices Claude offers (2 to 4), shown as buttons
    char opts[4][120];
} event_t;

typedef struct {
    char cmd[10];  // "decide", "say" or "status"
    int id;
    bool allow;
    char text[160];
} command_t;

#define COMMAND_SLOTS 4
#define APPROVAL_SHOWN_MS 60000

static char pairing_token[80];
static bool token_loaded = false;
static int last_id = -1;  // -1: the first answer only sets where we are, so old messages are not read out on every start
static event_t *queue = NULL;  // in external memory: the texts are long
static volatile int head = 0, tail = 0;  // written by this task, read by the main loop
// The small picture of the PC's screen: 4 bytes (width, height, little-endian) then width*height RGB565 pixels (see dev_bridge.py)
#define SHOT_MAX_BYTES (4 + 320 * 240 * 2)
static uint8_t *shot_buf = NULL;
static size_t shot_used = 0;
static volatile bool shot_wanted = false;  // a message said a picture is waiting
static volatile bool shot_ready = false;
static command_t commands[COMMAND_SLOTS];
static volatile int command_head = 0, command_tail = 0;
static volatile int open_ask = 0;  // the approval the screen is offering now
static volatile uint32_t open_since = 0;
static char relay_url[96];  // https://bug.headlessqa.com (the internet mailbox), used when the home server cannot be reached
static bool relay_loaded = false;
static int local_failures = 0;
static uint32_t relay_retry_ms = 0;  // while on the mailbox: when to try the home server again
static bool on_relay = false;
static volatile bool choice_open = false;  // a list of choices is on the screen
static int last_nopts = 0;
static char last_opts[4][120];
static char last_full[1000];
static char *body = NULL;
static size_t body_used = 0;

static void load_token(void) {
    if (token_loaded) return;
    token_loaded = true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(pairing_token);
        if (nvs_get_str(handle, "devtok", pairing_token, &size) != ESP_OK) pairing_token[0] = '\0';
        nvs_close(handle);
    }
}

static void load_relay(void) {
    if (relay_loaded) return;
    relay_loaded = true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(relay_url);
        if (nvs_get_str(handle, "relay", relay_url, &size) != ESP_OK) relay_url[0] = 0;
        nvs_close(handle);
    }
}

bool dev_bridge_relay_configured(void) {
    load_relay();
    return relay_url[0] != 0;
}

// Only https:// addresses are accepted: the mailbox is on the internet. An empty one removes it.
void dev_bridge_set_relay(const char *url) {
    relay_loaded = true;
    if (url && url[0] && strncmp(url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "The mailbox address must start with https:// (ignored)");
        return;
    }
    strlcpy(relay_url, url ? url : "", sizeof(relay_url));
    size_t n = strlen(relay_url);
    while (n > 0 && relay_url[n - 1] == '/') relay_url[--n] = 0;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "relay", relay_url);
        nvs_commit(handle);
        nvs_close(handle);
    }
    local_failures = 0;
    ESP_LOGI(TAG, "%s", relay_url[0] ? "Internet mailbox set" : "Internet mailbox removed");
}

bool dev_bridge_paired(void) {
    load_token();
    return pairing_token[0] != '\0';
}

void dev_bridge_set_token(const char *value) {
    token_loaded = true;
    strlcpy(pairing_token, value ? value : "", sizeof(pairing_token));
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "devtok", pairing_token);
        nvs_commit(handle);
        nvs_close(handle);
    }
    last_id = -1;
    ESP_LOGI(TAG, "%s", pairing_token[0] ? "Paired with the PC" : "Pairing removed");
}

static void hex(const unsigned char *in, size_t n, char *out) {
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = DIGITS[in[i] >> 4];
        out[2 * i + 1] = DIGITS[in[i] & 15];
    }
    out[2 * n] = '\0';
}

// X-Bug-Auth: "<unix time>:<hmac>" for a request
static bool sign(const char *method, const char *path, char *out, size_t size) {
    time_t now = time(NULL);
    if (now < 1700000000 || size < 100) return false;  // the clock is not set yet
    char message[160];
    snprintf(message, sizeof(message), "%lld:%s:%s", (long long)now, method, path);
    unsigned char mac[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const unsigned char *)pairing_token, strlen(pairing_token),
                        (const unsigned char *)message, strlen(message), mac) != 0) {
        return false;
    }
    char digest[65];
    hex(mac, sizeof(mac), digest);
    snprintf(out, size, "%lld:%s", (long long)now, digest);
    return true;
}

static esp_err_t on_http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0 && body && body_used + (size_t)event->data_len < BODY_BYTES) {
        memcpy(body + body_used, event->data, (size_t)event->data_len);
        body_used += (size_t)event->data_len;
        body[body_used] = '\0';
    }
    return ESP_OK;
}

static int level_of(const char *name) {
    if (!name) return 0;
    return strcmp(name, "ok") == 0 ? 1 : strcmp(name, "warn") == 0 ? 2 : strcmp(name, "error") == 0 ? 3 : 0;
}

static void handle_answer(void) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return;
    cJSON *latest = cJSON_GetObjectItem(root, "latest");
    cJSON *list = cJSON_GetObjectItem(root, "events");
    if (last_id >= 0 && cJSON_IsNumber(latest) && latest->valueint < last_id) last_id = -1;  // the PC server restarted and numbers began again
    bool first_contact = last_id < 0;  // then only messages from the last two minutes are news
    if (first_contact) last_id = cJSON_IsNumber(latest) ? latest->valueint : 0;
    cJSON *item;
    cJSON_ArrayForEach(item, list) {
        cJSON *id = cJSON_GetObjectItem(item, "id"), *text = cJSON_GetObjectItem(item, "text");
        cJSON *level = cJSON_GetObjectItem(item, "level"), *speak = cJSON_GetObjectItem(item, "speak");
        if (!cJSON_IsNumber(id) || !cJSON_IsString(text)) continue;
        cJSON *age = cJSON_GetObjectItem(item, "age");
        if (first_contact && cJSON_IsNumber(age) && age->valueint > 120) continue;
        if (id->valueint > last_id) last_id = id->valueint;
        int next = (head + 1) % QUEUE_SIZE;
        if (next == tail) continue;  // the screen has not caught up: drop the newest
        strlcpy(queue[head].text, text->valuestring, sizeof(queue[head].text));
        queue[head].level = level_of(cJSON_IsString(level) ? level->valuestring : NULL);
        queue[head].speak = !cJSON_IsFalse(speak);
        cJSON *ask = cJSON_GetObjectItem(item, "ask");
        queue[head].ask = cJSON_IsNumber(ask) ? ask->valueint : 0;
        queue[head].nopts = 0;
        cJSON *shot_flag = cJSON_GetObjectItem(item, "shot");
        if (cJSON_IsTrue(shot_flag)) shot_wanted = true;
        cJSON *full = cJSON_GetObjectItem(item, "full");
        strlcpy(queue[head].full, cJSON_IsString(full) ? full->valuestring : "", sizeof(queue[head].full));
        cJSON *options = cJSON_GetObjectItem(item, "options"), *option;
        cJSON_ArrayForEach(option, options) {
            if (cJSON_IsString(option) && queue[head].nopts < 4) strlcpy(queue[head].opts[queue[head].nopts++], option->valuestring, 120);
        }
        head = next;
        ESP_LOGI(TAG, "Message %d from the PC: %s", id->valueint, text->valuestring);
    }
    cJSON_Delete(root);
}

static esp_err_t on_shot_data(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0 && shot_buf && shot_used + (size_t)event->data_len <= SHOT_MAX_BYTES) {
        memcpy(shot_buf + shot_used, event->data, (size_t)event->data_len);
        shot_used += (size_t)event->data_len;
    }
    return ESP_OK;
}

// Downloads the picture from the PC (at home) or from the internet mailbox (away), then lets the screen show it.
static void fetch_shot(const char *base, bool secure) {
    shot_wanted = false;
    if (!shot_buf) shot_buf = heap_caps_malloc(SHOT_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!shot_buf) return;
    char url[200], auth[120];
    snprintf(url, sizeof(url), "%s/dev/shot", base);
    if (!sign("GET", "/dev/shot", auth, sizeof(auth))) return;
    shot_used = 0;
    esp_http_client_config_t config = {.url = url, .timeout_ms = 20000, .buffer_size = 2048, .event_handler = on_shot_data};
    if (secure) config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_http_client_set_header(client, "X-Bug-Auth", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    int w = shot_used >= 4 ? shot_buf[0] | (shot_buf[1] << 8) : 0, h = shot_used >= 4 ? shot_buf[2] | (shot_buf[3] << 8) : 0;
    if (status == 200 && w >= 1 && w <= 320 && h >= 1 && h <= 240 && shot_used == (size_t)(4 + w * h * 2)) {
        shot_ready = true;
        ESP_LOGI(TAG, "Picture of the PC screen received (%dx%d)", w, h);
    } else {
        ESP_LOGW(TAG, "The picture could not be fetched (status %d, %u bytes)", status, (unsigned)shot_used);
    }
}

bool dev_bridge_shot_take(const uint8_t **pixels, int *w, int *h) {
    if (!shot_ready) return false;
    shot_ready = false;
    *w = shot_buf[0] | (shot_buf[1] << 8);
    *h = shot_buf[2] | (shot_buf[3] << 8);
    *pixels = shot_buf + 4;
    return true;
}

// Sends one order to the PC (the answer arrives later as a normal message that the board says).
static void post_command(const command_t *command, const char *server_url) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", command->cmd);
    if (command->id > 0) cJSON_AddNumberToObject(root, "id", command->id);
    cJSON_AddBoolToObject(root, "allow", command->allow);
    if (command->text[0]) cJSON_AddStringToObject(root, "text", command->text);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;
    char url[200], auth[120];
    snprintf(url, sizeof(url), "%s/dev/command", server_url);
    if (sign("POST", "/dev/command", auth, sizeof(auth))) {
        body_used = 0;
        body[0] = 0;
        esp_http_client_config_t config = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 8000, .buffer_size = 1024, .event_handler = on_http_event};
        if (strncmp(url, "https://", 8) == 0) config.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client) {
            esp_http_client_set_header(client, "X-Bug-Auth", auth);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, json, (int)strlen(json));
            esp_err_t err = esp_http_client_perform(client);
            ESP_LOGI(TAG, "Order '%s' to the PC: %s", command->cmd, err == ESP_OK ? "sent" : esp_err_to_name(err));
            esp_http_client_cleanup(client);
        }
    }
    free(json);
}

static void dev_task(void *arg) {
    for (;;) {
        if (!dev_bridge_paired() || network_manager_is_connected() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        bug_settings_t settings;
        settings_copy(&settings);
        load_relay();
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        // Home server first. After three failures in a row the internet mailbox is used for three minutes, then the home server is tried again.
        bool use_relay = relay_url[0] && local_failures >= 3 && (int32_t)(now_ms - relay_retry_ms) < 0;
        const char *base = use_relay ? relay_url : settings.server_url;
        if (use_relay != on_relay) {
            on_relay = use_relay;
            last_id = -1;  // another source numbers its messages on its own
            ESP_LOGI(TAG, "%s", use_relay ? "Away from the home server: using the internet mailbox" : "Trying the home server");
        }
        while (command_tail != command_head) {  // orders from the screen or the voice go first
            command_t order = commands[command_tail];
            command_tail = (command_tail + 1) % COMMAND_SLOTS;
            post_command(&order, base);
        }
        char path[64], url[200], auth[120];
        snprintf(path, sizeof(path), "/dev/events");
        if (!sign("GET", path, auth, sizeof(auth))) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        snprintf(url, sizeof(url), "%s%s?since=%d&wait=%d", base, path, last_id < 0 ? 0 : last_id, use_relay ? 8 : 4);
        body_used = 0;
        body[0] = '\0';
        esp_http_client_config_t config = {.url = url, .timeout_ms = 32000, .buffer_size = 1024, .event_handler = on_http_event};
        if (use_relay) config.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        esp_http_client_set_header(client, "X-Bug-Auth", auth);
        esp_err_t err = esp_http_client_perform(client);
        int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
        esp_http_client_cleanup(client);
        if (!use_relay) {
            if (status == 200 || status == 401) {
                local_failures = 0;
            } else if (local_failures < 100) {
                local_failures++;
            }
            if (local_failures >= 3 && relay_url[0]) relay_retry_ms = now_ms + 180000;
        }
        if (status == 200) {
            handle_answer();
            if (shot_wanted) fetch_shot(base, use_relay);
        } else if (status == 401) {
            ESP_LOGW(TAG, "The PC does not accept this pairing (wrong pairing_token or clocks far apart)");
            vTaskDelay(pdMS_TO_TICKS(60000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10000));  // the PC is off or away: try again later
        }
    }
}

bool dev_bridge_take(char *text, size_t size, int *level, bool *speak, int *ask) {
    if (tail == head) return false;
    strlcpy(text, queue[tail].text, size);
    *level = queue[tail].level;
    *speak = queue[tail].speak;
    *ask = queue[tail].ask;
    last_nopts = queue[tail].nopts;
    for (int i = 0; i < last_nopts; i++) strlcpy(last_opts[i], queue[tail].opts[i], 120);
    strlcpy(last_full, queue[tail].full, sizeof(last_full));
    if (last_nopts >= 2 || last_full[0]) choice_open = true;  // a list of choices, or a long answer to read: stays until closed
    if (*ask > 0) {
        open_ask = *ask;
        open_since = (uint32_t)(esp_timer_get_time() / 1000);
    }
    tail = (tail + 1) % QUEUE_SIZE;
    return true;
}

int dev_bridge_last_choices(char labels[][120]) {  // the choices of the message just taken, 0 if it had none
    for (int i = 0; i < last_nopts; i++) strlcpy(labels[i], last_opts[i], 120);
    return last_nopts;
}

const char *dev_bridge_last_full(void) {  // the whole explanation that came with the message just taken (may be empty)
    return last_full;
}

bool dev_bridge_choice_open(void) {
    return choice_open;
}

void dev_bridge_choice_close(void) {
    choice_open = false;
}

int dev_bridge_pending_ask(void) {
    if (open_ask > 0 && (uint32_t)(esp_timer_get_time() / 1000) - open_since > APPROVAL_SHOWN_MS) open_ask = 0;  // the PC stopped waiting
    return open_ask;
}

bool dev_bridge_send(const char *cmd, int id, bool allow, const char *text) {
    int next = (command_head + 1) % COMMAND_SLOTS;
    if (next == command_tail) return false;
    command_t *slot = &commands[command_head];
    strlcpy(slot->cmd, cmd, sizeof(slot->cmd));
    slot->id = id;
    slot->allow = allow;
    strlcpy(slot->text, text ? text : "", sizeof(slot->text));
    if (strcmp(cmd, "say") == 0) choice_open = false;  // answered, by touch or by voice
    command_head = next;
    return true;
}

bool dev_bridge_decide(int id, bool allow) {
    if (id <= 0) return false;
    if (id == open_ask) open_ask = 0;
    return dev_bridge_send("decide", id, allow, NULL);
}

void dev_bridge_start(void) {
    queue = heap_caps_calloc(QUEUE_SIZE, sizeof(event_t), MALLOC_CAP_SPIRAM);
    body = heap_caps_calloc(1, BODY_BYTES, MALLOC_CAP_SPIRAM);
    if (!queue || !body) {
        ESP_LOGE(TAG, "No memory for the dev bridge");
        return;
    }
    xTaskCreate(dev_task, "dev_bridge", 6144, NULL, 3, NULL);
}

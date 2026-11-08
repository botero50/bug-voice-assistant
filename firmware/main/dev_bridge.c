#include "dev_bridge.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "network_manager.h"
#include "nvs.h"
#include "settings_store.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// The dev bridge, board side (docs/DEV_BRIDGE_PLAN.md, phase 0). Tools on the PC (Claude Code hooks, scripts) post short messages to the
// PC server; this task asks the server for new ones with a long poll and hands them to the screen and the voice. Every request is signed
// with the pairing token (HMAC-SHA256 of "<time>:<method>:<path>"), which the board receives over the USB cable and keeps in its memory.

static const char *TAG = "DEV_BRIDGE";

#define QUEUE_SIZE 6
#define BODY_BYTES 1800

typedef struct {
    char text[140];
    int level;
    bool speak;
} event_t;

static char pairing_token[80];
static bool token_loaded = false;
static int last_id = -1;  // -1: the first answer only sets where we are, so old messages are not read out on every start
static event_t queue[QUEUE_SIZE];
static volatile int head = 0, tail = 0;  // written by this task, read by the main loop
static char body[BODY_BYTES];
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
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0 && body_used + (size_t)event->data_len < sizeof(body)) {
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
        head = next;
        ESP_LOGI(TAG, "Message %d from the PC: %s", id->valueint, text->valuestring);
    }
    cJSON_Delete(root);
}

static void dev_task(void *arg) {
    for (;;) {
        if (!dev_bridge_paired() || network_manager_is_connected() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        bug_settings_t settings;
        settings_copy(&settings);
        char path[64], url[200], auth[120];
        snprintf(path, sizeof(path), "/dev/events");
        if (!sign("GET", path, auth, sizeof(auth))) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        snprintf(url, sizeof(url), "%s%s?since=%d&wait=20", settings.server_url, path, last_id < 0 ? 0 : last_id);
        body_used = 0;
        body[0] = '\0';
        esp_http_client_config_t config = {.url = url, .timeout_ms = 32000, .buffer_size = 1024, .event_handler = on_http_event};
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        esp_http_client_set_header(client, "X-Bug-Auth", auth);
        esp_err_t err = esp_http_client_perform(client);
        int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
        esp_http_client_cleanup(client);
        if (status == 200) {
            handle_answer();
        } else if (status == 401) {
            ESP_LOGW(TAG, "The PC does not accept this pairing (wrong pairing_token or clocks far apart)");
            vTaskDelay(pdMS_TO_TICKS(60000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10000));  // the PC is off or away: try again later
        }
    }
}

bool dev_bridge_take(char *text, size_t size, int *level, bool *speak) {
    if (tail == head) return false;
    strlcpy(text, queue[tail].text, size);
    *level = queue[tail].level;
    *speak = queue[tail].speak;
    tail = (tail + 1) % QUEUE_SIZE;
    return true;
}

void dev_bridge_start(void) {
    xTaskCreate(dev_task, "dev_bridge", 6144, NULL, 3, NULL);
}

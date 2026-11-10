#include "voice_client.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "settings_store.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "VOICE_CLIENT";

#define MAX_REPLY_BYTES (1024 * 1024)
#define REQUEST_TIMEOUT_MS 12000  // a PC that does not answer in this long is treated as down

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    voice_action_t action;
    char heard[128];
    char reply[256];
    char screen[160];
    bool failed;
    int64_t t_connected, t_sent, t_header, t_finish;  // microseconds, for the timing log
} rx_state_t;

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Headers arrive percent-encoded ("Qu%C3%A9") so accents survive HTTP.
void voice_client_percent_decode(char *dst, size_t cap, const char *src) {
    size_t out = 0;
    while (*src && out + 1 < cap) {
        if (src[0] == '%' && hex_value(src[1]) >= 0 && hex_value(src[2]) >= 0) {
            dst[out++] = (char)(hex_value(src[1]) * 16 + hex_value(src[2]));
            src += 3;
        } else {
            dst[out++] = *src++;
        }
    }
    dst[out] = '\0';
}

static esp_err_t on_http_event(esp_http_client_event_t *evt) {
    rx_state_t *rx = (rx_state_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        rx->t_connected = esp_timer_get_time();
    } else if (evt->event_id == HTTP_EVENT_HEADERS_SENT) {
        rx->t_sent = esp_timer_get_time();
    } else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        rx->t_finish = esp_timer_get_time();
    }
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (!rx->t_header) rx->t_header = esp_timer_get_time();
        if (strcasecmp(evt->header_key, "X-Action") == 0) {
            if (strcmp(evt->header_value, "reply") == 0) {
                rx->action = VOICE_ACTION_REPLY;
            } else if (strcmp(evt->header_value, "wake") == 0) {
                rx->action = VOICE_ACTION_WAKE;
            } else if (strcmp(evt->header_value, "heard") == 0) {
                rx->action = VOICE_ACTION_HEARD;
            } else {
                rx->action = VOICE_ACTION_IGNORE;
            }
        } else if (strcasecmp(evt->header_key, "X-Heard") == 0) {
            voice_client_percent_decode(rx->heard, sizeof(rx->heard), evt->header_value);
        } else if (strcasecmp(evt->header_key, "X-Reply") == 0) {
            voice_client_percent_decode(rx->reply, sizeof(rx->reply), evt->header_value);
        } else if (strcasecmp(evt->header_key, "X-Screen") == 0) {
            voice_client_percent_decode(rx->screen, sizeof(rx->screen), evt->header_value);
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t need = rx->len + evt->data_len;
        if (need > MAX_REPLY_BYTES) {
            rx->failed = true;
            return ESP_FAIL;
        }
        if (need > rx->cap) {
            size_t cap = rx->cap ? rx->cap : 64 * 1024;
            while (cap < need) cap *= 2;
            uint8_t *grown = heap_caps_realloc(rx->data, cap, MALLOC_CAP_SPIRAM);
            if (!grown) {
                rx->failed = true;
                return ESP_ERR_NO_MEM;
            }
            rx->data = grown;
            rx->cap = cap;
        }
        memcpy(rx->data + rx->len, evt->data, evt->data_len);
        rx->len += evt->data_len;
    }
    return ESP_OK;
}

static esp_err_t send_request(const char *url_path, const void *body, size_t body_bytes, voice_result_t *result);

esp_err_t voice_client_listen(bool command_mode, const int16_t *pcm, size_t pcm_bytes, voice_result_t *result) {
    char path[48];
    snprintf(path, sizeof(path), "listen?mode=%s&local=1", command_mode ? "command" : "wake");
    return send_request(path, pcm, pcm_bytes, result);
}

esp_err_t voice_client_ask(const char *text, voice_result_t *result) {
    return send_request("ask", text, strlen(text), result);
}

static esp_err_t send_request(const char *url_path, const void *pcm, size_t pcm_bytes, voice_result_t *result) {
    memset(result, 0, sizeof(*result));
    result->action = VOICE_ACTION_ERROR;

    rx_state_t rx = {.action = VOICE_ACTION_ERROR};
    bug_settings_t settings;
    settings_copy(&settings);
    char url[200];
    snprintf(url, sizeof(url), "%s/%s", settings.server_url, url_path);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http_event,
        .user_data = &rx,
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .buffer_size = 2048,  // headers carry the percent-encoded text
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)pcm, (int)pcm_bytes);

    int64_t started_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    wifi_ap_record_t ap = {0};
    esp_wifi_sta_get_ap_info(&ap);
    ESP_LOGI(TAG, "Phases (ms after start): connected %lld, request sent %lld, first header %lld, finished %lld",
             (long long)((rx.t_connected - started_us) / 1000), (long long)((rx.t_sent - started_us) / 1000),
             (long long)((rx.t_header - started_us) / 1000), (long long)((rx.t_finish - started_us) / 1000));
    ESP_LOGI(TAG, "Server round trip: %lld ms (sent %u bytes, received %u bytes, Wi-Fi signal %d dBm, free internal RAM %u)",
             (long long)((esp_timer_get_time() - started_us) / 1000), (unsigned)pcm_bytes, (unsigned)rx.len, ap.rssi,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    if (err != ESP_OK || status != 200 || rx.failed || rx.action == VOICE_ACTION_ERROR) {
        ESP_LOGW(TAG, "Server request failed: %s, HTTP %d", esp_err_to_name(err), status);
        heap_caps_free(rx.data);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    result->action = rx.action;
    memcpy(result->heard, rx.heard, sizeof(result->heard));
    memcpy(result->reply, rx.reply, sizeof(result->reply));
    memcpy(result->screen, rx.screen, sizeof(result->screen));
    result->audio = (int16_t *)rx.data;
    result->audio_bytes = rx.len & ~(size_t)1;
    return ESP_OK;
}

void voice_client_bench(void) {
    bug_settings_t settings;
    settings_copy(&settings);
    char url[200];

    // Download: 200 KB
    snprintf(url, sizeof(url), "%s/bench?kb=200", settings.server_url);
    rx_state_t rx = {.action = VOICE_ACTION_IGNORE};
    esp_http_client_config_t down = {.url = url, .event_handler = on_http_event, .user_data = &rx, .timeout_ms = 10000,
                                     .buffer_size = 2048};
    esp_http_client_handle_t client = esp_http_client_init(&down);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = client ? esp_http_client_perform(client) : ESP_FAIL;
    int64_t down_us = esp_timer_get_time() - t0;
    if (client) esp_http_client_cleanup(client);
    size_t got = rx.len;
    heap_caps_free(rx.data);

    // Upload: 100 KB
    snprintf(url, sizeof(url), "%s/bench", settings.server_url);
    size_t up_bytes = 100 * 1024;
    uint8_t *junk = heap_caps_calloc(1, up_bytes, MALLOC_CAP_SPIRAM);
    int64_t up_us = 0;
    if (junk) {
        esp_http_client_config_t up = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 10000};
        esp_http_client_handle_t poster = esp_http_client_init(&up);
        if (poster) {
            esp_http_client_set_post_field(poster, (const char *)junk, (int)up_bytes);
            int64_t t1 = esp_timer_get_time();
            esp_http_client_perform(poster);
            up_us = esp_timer_get_time() - t1;
            esp_http_client_cleanup(poster);
        }
        heap_caps_free(junk);
    }
    ESP_LOGI(TAG, "Network test: download %u KB in %lld ms (%lld KB/s, %s), upload %u KB in %lld ms (%lld KB/s)",
             (unsigned)(got / 1024), (long long)(down_us / 1000), down_us ? (long long)(got * 1000000LL / 1024 / down_us) : 0,
             esp_err_to_name(err), (unsigned)(up_bytes / 1024), (long long)(up_us / 1000),
             up_us ? (long long)(up_bytes * 1000000LL / 1024 / up_us) : 0);
}

esp_err_t voice_client_train_next(char *label, size_t label_size, char *text, size_t text_size, bool *done, bool *raw) {
    char *json = NULL;
    esp_err_t err = voice_client_fetch_screen("train_next", &json);
    if (err != ESP_OK || !json) {
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(json);
    heap_caps_free(json);
    if (!root) {
        return ESP_FAIL;
    }
    cJSON *finished = cJSON_GetObjectItem(root, "done");
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *phrase = cJSON_GetObjectItem(root, "text");
    *done = cJSON_IsTrue(finished);
    if (raw) *raw = cJSON_IsTrue(cJSON_GetObjectItem(root, "raw"));
    if (!*done && cJSON_IsString(id) && cJSON_IsString(phrase)) {
        strlcpy(label, id->valuestring, label_size);
        strlcpy(text, phrase->valuestring, text_size);
    }
    cJSON_Delete(root);
    return *done || label[0] ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_client_train_raw(const char *label, const int16_t *stereo32k, size_t bytes) {
    bug_settings_t settings;
    settings_copy(&settings);
    char url[200];
    snprintf(url, sizeof(url), "%s/train/clip?label=%s&raw=1", settings.server_url, label);
    esp_http_client_config_t config = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 30000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)stereo32k, (int)bytes);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200 ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_client_train_clip(const char *label, const int16_t *pcm, size_t pcm_bytes) {
    bug_settings_t settings;
    settings_copy(&settings);
    char url[200];
    snprintf(url, sizeof(url), "%s/train/clip?label=%s", settings.server_url, label);
    esp_http_client_config_t config = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 10000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)pcm, (int)pcm_bytes);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200 ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_client_fetch_screen(const char *path, char **json) {
    *json = NULL;

    bug_settings_t settings;
    settings_copy(&settings);
    char url[200];
    snprintf(url, sizeof(url), "%s/screen/%s", settings.server_url, path);

    rx_state_t rx = {.action = VOICE_ACTION_IGNORE};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_http_event,
        .user_data = &rx,
        .timeout_ms = 10000,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || rx.failed || rx.len == 0) {
        ESP_LOGW(TAG, "Could not fetch screen %s: %s, HTTP %d", path, esp_err_to_name(err), status);
        heap_caps_free(rx.data);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    char *text = heap_caps_realloc(rx.data, rx.len + 1, MALLOC_CAP_SPIRAM);
    if (!text) {
        heap_caps_free(rx.data);
        return ESP_ERR_NO_MEM;
    }
    text[rx.len] = '\0';
    *json = text;
    return ESP_OK;
}

void voice_result_free(voice_result_t *result) {
    heap_caps_free(result->audio);
    memset(result, 0, sizeof(*result));
}

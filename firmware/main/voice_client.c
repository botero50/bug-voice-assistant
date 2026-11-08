#include "voice_client.h"
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
#define REQUEST_TIMEOUT_MS 30000

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    voice_action_t action;
    char heard[128];
    char reply[256];
    char screen[160];
    bool failed;
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

    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(evt->header_key, "X-Action") == 0) {
            if (strcmp(evt->header_value, "reply") == 0) {
                rx->action = VOICE_ACTION_REPLY;
            } else if (strcmp(evt->header_value, "wake") == 0) {
                rx->action = VOICE_ACTION_WAKE;
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

esp_err_t voice_client_listen(bool command_mode, const int16_t *pcm, size_t pcm_bytes, voice_result_t *result) {
    memset(result, 0, sizeof(*result));
    result->action = VOICE_ACTION_ERROR;

    rx_state_t rx = {.action = VOICE_ACTION_ERROR};
    bug_settings_t settings;
    settings_copy(&settings);
    char url[160];
    snprintf(url, sizeof(url), "%s/listen?mode=%s", settings.server_url, command_mode ? "command" : "wake");

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

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

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

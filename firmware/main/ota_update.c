#include "mem_report.h"
#include "ota_update.h"
#include "music_player.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "OTA";

#define SITE "https://botero50.github.io/bug-voice-assistant/"
#define MANIFEST_URL SITE "manifest.json"
#define APP_URL SITE "firmware/app.bin"

static volatile bool running = false;
static char message[64] = "";
static volatile bool message_ready = false;

static void say(const char *text) {
    ESP_LOGI(TAG, "%s", text);
    strlcpy(message, text, sizeof(message));
    message_ready = true;
}

bool ota_take_message(char *out, size_t size) {
    if (!message_ready) {
        return false;
    }
    strlcpy(out, message, size);
    message_ready = false;
    return true;
}

void ota_mark_started_ok(void) {
    esp_ota_mark_app_valid_cancel_rollback();
}

// "0.5.0-preview" -> 0, 5, 0. Anything missing counts as 0.
static void parse_version(const char *text, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    sscanf(text, "%d.%d.%d", &out[0], &out[1], &out[2]);
}

static bool is_newer(const char *candidate, const char *current) {
    int a[3], b[3];
    parse_version(candidate, a);
    parse_version(current, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

// The version the page offers, copied into `version`.
static bool fetch_version(char *version, size_t size) {
    esp_http_client_config_t config = {
        .url = MANIFEST_URL,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int length = esp_http_client_fetch_headers(client);
        char body[512];
        int got = esp_http_client_read_response(client, body, sizeof(body) - 1);
        (void)length;
        if (got > 0 && esp_http_client_get_status_code(client) == 200) {
            body[got] = '\0';
            cJSON *root = cJSON_Parse(body);
            cJSON *item = root ? cJSON_GetObjectItem(root, "version") : NULL;
            if (cJSON_IsString(item) && item->valuestring) {
                strlcpy(version, item->valuestring, size);
                ok = true;
            }
            cJSON_Delete(root);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static void ota_task(void *arg) {
    mem_task_note("ota", 10240);
    const char *current = esp_ota_get_app_description()->version;
    char offered[32] = "";
    say("Buscando actualización...");

    if (!fetch_version(offered, sizeof(offered))) {
        say("No pude consultar la actualización");
    } else if (!is_newer(offered, current)) {
        say("Ya tienes la última versión");
    } else {
        char text[64];
        snprintf(text, sizeof(text), "Descargando versión %s...", offered);
        say(text);
        music_stop();

        esp_http_client_config_t http = {
            .url = APP_URL,
            .timeout_ms = 15000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = true,
        };
        esp_https_ota_config_t config = {.http_config = &http};
        esp_https_ota_handle_t handle = NULL;
        esp_err_t err = esp_https_ota_begin(&config, &handle);
        int last_percent = -1;
        while (err == ESP_OK) {
            err = esp_https_ota_perform(handle);
            if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                break;
            }
            int total = esp_https_ota_get_image_size(handle);
            int read = esp_https_ota_get_image_len_read(handle);
            int percent = total > 0 ? read * 100 / total : 0;
            if (percent / 20 != last_percent / 20) {
                last_percent = percent;
                snprintf(text, sizeof(text), "Descargando... %d%%", percent);
                say(text);
            }
            err = ESP_ERR_HTTPS_OTA_IN_PROGRESS == err ? ESP_OK : err;
        }
        if (err == ESP_OK && handle && esp_https_ota_is_complete_data_received(handle) &&
            esp_https_ota_finish(handle) == ESP_OK) {
            say("Actualizado. Reiniciando...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            if (handle) {
                esp_https_ota_abort(handle);
            }
            ESP_LOGE(TAG, "The update failed: %s", esp_err_to_name(err));
            say("La actualización falló");
        }
    }
    running = false;
    vTaskDelete(NULL);
}

void ota_update_start(void) {
    if (running) {
        return;
    }
    running = true;
    if (xTaskCreatePinnedToCore(ota_task, "ota", 10240, NULL, 4, NULL, 1) != pdPASS) {
        running = false;
        say("No hay memoria para actualizar");
    }
}

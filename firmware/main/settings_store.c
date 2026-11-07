#include "settings_store.h"
#include "bug_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "SETTINGS";
static const char *NAMESPACE = "bug";

static SemaphoreHandle_t lock = NULL;
static bug_settings_t current;

static void save_string(const char *key, const char *value) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s", key);
        return;
    }
    nvs_set_str(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

static void save_u8(const char *key, uint8_t value) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s", key);
        return;
    }
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

void settings_load(void) {
    lock = xSemaphoreCreateMutex();

    memset(&current, 0, sizeof(current));
    strlcpy(current.wifi_ssid, BUG_WIFI_SSID, sizeof(current.wifi_ssid));
    strlcpy(current.wifi_password, BUG_WIFI_PASSWORD, sizeof(current.wifi_password));
    strlcpy(current.server_url, BUG_SERVER_URL, sizeof(current.server_url));
    current.brightness = 70;
    current.volume = 80;
    current.mic_muted = false;

    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t length = sizeof(current.wifi_ssid);
        nvs_get_str(handle, "ssid", current.wifi_ssid, &length);
        length = sizeof(current.wifi_password);
        nvs_get_str(handle, "pass", current.wifi_password, &length);
        length = sizeof(current.server_url);
        nvs_get_str(handle, "server", current.server_url, &length);
        nvs_get_u8(handle, "bright", &current.brightness);
        nvs_get_u8(handle, "volume", &current.volume);
        uint8_t muted = 0;
        if (nvs_get_u8(handle, "muted", &muted) == ESP_OK) {
            current.mic_muted = muted != 0;
        }
        nvs_close(handle);
    }
    if (current.brightness < 10) current.brightness = 10;
    if (current.brightness > 100) current.brightness = 100;
    if (current.volume > 100) current.volume = 100;

    ESP_LOGI(TAG, "Loaded: wifi %s, server %s, brightness %d, volume %d, mic %s",
             current.wifi_ssid[0] ? "set" : "not set", current.server_url, current.brightness, current.volume,
             current.mic_muted ? "muted" : "on");
}

void settings_copy(bug_settings_t *out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = current;
    xSemaphoreGive(lock);
}

void settings_set_wifi(const char *ssid, const char *password) {
    xSemaphoreTake(lock, portMAX_DELAY);
    strlcpy(current.wifi_ssid, ssid, sizeof(current.wifi_ssid));
    strlcpy(current.wifi_password, password, sizeof(current.wifi_password));
    xSemaphoreGive(lock);
    save_string("ssid", ssid);
    save_string("pass", password);
}

void settings_set_server_url(const char *url) {
    xSemaphoreTake(lock, portMAX_DELAY);
    strlcpy(current.server_url, url, sizeof(current.server_url));
    xSemaphoreGive(lock);
    save_string("server", url);
}

void settings_set_brightness(uint8_t percent) {
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    xSemaphoreTake(lock, portMAX_DELAY);
    current.brightness = percent;
    xSemaphoreGive(lock);
    save_u8("bright", percent);
}

void settings_set_volume(uint8_t percent) {
    if (percent > 100) percent = 100;
    xSemaphoreTake(lock, portMAX_DELAY);
    current.volume = percent;
    xSemaphoreGive(lock);
    save_u8("volume", percent);
}

void settings_set_mic_muted(bool muted) {
    xSemaphoreTake(lock, portMAX_DELAY);
    current.mic_muted = muted;
    xSemaphoreGive(lock);
    save_u8("muted", muted ? 1 : 0);
}

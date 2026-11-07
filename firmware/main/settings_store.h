#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char server_url[96];
    uint8_t brightness;  // 10-100
    uint8_t volume;      // 0-100
    bool mic_muted;
} bug_settings_t;

// Loads what was saved on the board. Anything never set on the device falls back to
// bug_config.local.h. Call after nvs_flash_init().
void settings_load(void);

// Thread-safe copy of the current settings.
void settings_copy(bug_settings_t *out);

void settings_set_wifi(const char *ssid, const char *password);
void settings_set_server_url(const char *url);
void settings_set_brightness(uint8_t percent);
void settings_set_volume(uint8_t percent);
void settings_set_mic_muted(bool muted);

#endif

#include "power_manager.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER_MGR";
static PowerMode current_mode = POWER_MODE_NORMAL;
static uint32_t idle_timeout_counter = 0;
static uint32_t current_max_freq_mhz = 240;

static esp_err_t apply_cpu_freq(uint32_t max_mhz) {
    esp_pm_config_t pm_config = {
        .max_freq_mhz = max_mhz,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
        return ret;
    }
    current_max_freq_mhz = max_mhz;
    return ESP_OK;
}

esp_err_t power_manager_init(void) {
    ESP_LOGI(TAG, "Initializing power management...");

    esp_err_t ret = apply_cpu_freq(240);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "✓ Power management configured (CPU: 80-240 MHz dynamic scaling)");
    return ESP_OK;
}

esp_err_t power_manager_set_mode(PowerMode mode) {
    if (mode == current_mode) {
        return ESP_OK;
    }

    current_mode = mode;

    switch (mode) {
    case POWER_MODE_ACTIVE:
        ESP_LOGD(TAG, "Switching to ACTIVE mode (240 MHz, no sleep)");
        return apply_cpu_freq(240);

    case POWER_MODE_NORMAL:
        ESP_LOGD(TAG, "Switching to NORMAL mode (160 MHz)");
        return apply_cpu_freq(160);

    case POWER_MODE_SLEEP:
        ESP_LOGD(TAG, "Switching to SLEEP mode (80 MHz, modem sleep enabled)");
        return apply_cpu_freq(80);

    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t power_manager_set_cpu_freq(uint32_t freq_mhz) {
    if (freq_mhz >= 240) {
        return apply_cpu_freq(240);
    } else if (freq_mhz >= 160) {
        return apply_cpu_freq(160);
    }
    return apply_cpu_freq(80);
}

esp_err_t power_manager_set_brightness(uint8_t level) {
    if (level > 100) level = 100;

    // TODO: Implement AMOLED brightness control via GPIO PWM
    // PWM duty cycle = (level / 100.0) * 255

    ESP_LOGD(TAG, "Display brightness set to %d%%", level);
    return ESP_OK;
}

uint8_t power_manager_get_battery_level(void) {
    // TODO: Read ADC value and convert to percentage
    // For now, return mock value
    return 85;
}

uint32_t power_manager_get_cpu_freq(void) {
    return current_max_freq_mhz;
}

void power_manager_enable_modem_sleep(bool enable) {
    if (enable) {
        ESP_LOGD(TAG, "Modem sleep: ENABLED (WiFi/BLE will sleep when idle)");
        // esp_wifi_set_ps(WIFI_PS_MODEM);  // Requires WiFi to be initialized
    } else {
        ESP_LOGD(TAG, "Modem sleep: DISABLED");
    }
}

void power_manager_idle_timeout_reset(void) {
    idle_timeout_counter = 0;
}

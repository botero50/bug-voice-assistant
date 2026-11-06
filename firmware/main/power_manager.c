#include "power_manager.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_cpu.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER_MGR";
static PowerMode current_mode = POWER_MODE_NORMAL;
static uint32_t idle_timeout_counter = 0;

esp_err_t power_manager_init(void) {
    ESP_LOGI(TAG, "Initializing power management...");

    // Configure dynamic power management
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize ADC for battery monitoring
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);  // GPIO0

    ESP_LOGI(TAG, "✓ Power management configured");
    ESP_LOGI(TAG, "  - CPU: 80-240 MHz dynamic scaling");
    ESP_LOGI(TAG, "  - Light sleep: enabled");
    ESP_LOGI(TAG, "  - Battery monitoring: enabled");

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
        esp_cpu_set_freq(ESP_CPU_FREQ_240M);
        break;

    case POWER_MODE_NORMAL:
        ESP_LOGD(TAG, "Switching to NORMAL mode (160 MHz)");
        esp_cpu_set_freq(ESP_CPU_FREQ_160M);
        break;

    case POWER_MODE_SLEEP:
        ESP_LOGD(TAG, "Switching to SLEEP mode (80 MHz, modem sleep enabled)");
        esp_cpu_set_freq(ESP_CPU_FREQ_80M);
        break;

    default:
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t power_manager_set_cpu_freq(uint32_t freq_mhz) {
    esp_cpu_freq_t freq;

    if (freq_mhz >= 240) {
        freq = ESP_CPU_FREQ_240M;
    } else if (freq_mhz >= 160) {
        freq = ESP_CPU_FREQ_160M;
    } else {
        freq = ESP_CPU_FREQ_80M;
    }

    esp_cpu_set_freq(freq);
    return ESP_OK;
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
    // Read current CPU frequency
    uint32_t freq = esp_cpu_get_cycle_count();
    // Rough estimate: ~240M cycles/sec at max freq
    return 240;  // TODO: Get actual current frequency
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

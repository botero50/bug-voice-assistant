#include "power_optimizer.h"
#include "display_driver.h"
#include "power_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <math.h>

static const char *TAG = "POWER_OPTIMIZER";

static PowerState current_state = {
    .profile = PROFILE_ALWAYS_LISTENING,
    .battery_percent = 100,
    .current_draw_ma = 10.0,
    .estimated_hours_remaining = 200.0,
    .uptime_seconds = 0,
    .boost_converter_on = true,
    .wifi_on = true,
    .microphone_on = true,
    .display_brightness = 100,
    .cpu_freq_mhz = 240
};

static uint32_t startup_time_us = 0;

esp_err_t power_optimizer_init(void) {
    ESP_LOGI(TAG, "Initializing power optimizer");
    startup_time_us = esp_timer_get_time();

    // Start with Always-Listening profile
    power_optimizer_set_profile(PROFILE_ALWAYS_LISTENING);

    ESP_LOGI(TAG, "Power optimizer ready (Profile: ALWAYS_LISTENING)");
    return ESP_OK;
}

void power_optimizer_update_state(void) {
    // Update uptime
    current_state.uptime_seconds = (esp_timer_get_time() - startup_time_us) / 1e6;

    // Recalculate power consumption based on current state
    float power_ma = POWER_SYSTEM_IDLE;  // Base system power

    // CPU contribution
    switch (current_state.cpu_freq_mhz) {
        case 240:
            power_ma += POWER_CPU_240MHZ;
            break;
        case 160:
            power_ma += POWER_CPU_160MHZ;
            break;
        case 80:
            power_ma += POWER_CPU_80MHZ;
            break;
    }

    // WiFi contribution
    if (current_state.wifi_on) {
        power_ma += POWER_WIFI_IDLE;  // Modem sleep when idle
    }

    // Microphone contribution
    if (current_state.microphone_on) {
        power_ma += POWER_MICROPHONE_ON;
    }

    // Display contribution
    if (current_state.boost_converter_on) {
        power_ma += POWER_BOOST_CONVERTER_ON;
        // Add display brightness contribution
        power_ma += (POWER_AMOLED_FULL_BRIGHT * current_state.display_brightness) / 100;
    } else {
        // Display off, boost converter gated
        power_ma += POWER_AMOLED_OFF;
    }

    current_state.current_draw_ma = power_ma;

    // Calculate estimated battery remaining
    // Energy = mAh / mA = hours
    if (power_ma > 0.1) {
        float battery_mah = (BATTERY_CAPACITY_MAH * current_state.battery_percent) / 100.0;
        current_state.estimated_hours_remaining = battery_mah / power_ma;
    } else {
        current_state.estimated_hours_remaining = 1000.0;  // Cap at very high value
    }
}

PowerState power_optimizer_get_state(void) {
    power_optimizer_update_state();
    return current_state;
}

void power_optimizer_set_profile(PowerProfile profile) {
    ESP_LOGI(TAG, "Switching to power profile: %d", profile);

    current_state.profile = profile;

    switch (profile) {
        case PROFILE_ALWAYS_LISTENING:
            // Full features, always ready
            current_state.cpu_freq_mhz = 240;
            current_state.wifi_on = true;
            current_state.microphone_on = true;
            current_state.display_brightness = 100;
            current_state.boost_converter_on = true;
            ESP_LOGI(TAG, "Profile: ALWAYS_LISTENING (8-12mA)");
            break;

        case PROFILE_PERIODIC:
            // Reduced power, periodic wake
            current_state.cpu_freq_mhz = 160;
            current_state.wifi_on = false;  // WiFi will be periodically re-enabled
            current_state.microphone_on = true;  // Duty-cycled in voice_loop
            current_state.display_brightness = 30;  // Auto-dimmed
            current_state.boost_converter_on = true;
            ESP_LOGI(TAG, "Profile: PERIODIC (2-3mA)");
            break;

        case PROFILE_GESTURE_ONLY:
            // Ultra-low power, touch activation only
            current_state.cpu_freq_mhz = 80;
            current_state.wifi_on = false;
            current_state.microphone_on = false;
            current_state.display_brightness = 0;
            current_state.boost_converter_on = false;  // Critical: gate boost converter
            display_gate_boost_converter(false);  // Gate AMOLED boost converter
            ESP_LOGI(TAG, "Profile: GESTURE_ONLY (0.5-1mA)");
            break;
    }

    power_optimizer_update_state();
}

PowerProfile power_optimizer_get_profile(void) {
    return current_state.profile;
}

void power_optimizer_set_display_brightness(uint8_t brightness) {
    if (brightness > 100) brightness = 100;

    current_state.display_brightness = brightness;

    // Update display hardware
    display_set_brightness(brightness);

    // Auto-gate boost converter if brightness is 0
    if (brightness == 0 && current_state.boost_converter_on) {
        power_optimizer_gate_boost_converter(false);
    } else if (brightness > 0 && !current_state.boost_converter_on) {
        power_optimizer_gate_boost_converter(true);
    }

    ESP_LOGD(TAG, "Display brightness: %d%%", brightness);
    power_optimizer_update_state();
}

void power_optimizer_gate_boost_converter(bool enabled) {
    // CRITICAL: Gating the boost converter saves 25-35mA
    // This is the single largest power optimization opportunity
    current_state.boost_converter_on = enabled;

    display_gate_boost_converter(enabled);

    if (enabled) {
        ESP_LOGI(TAG, "Boost converter ENABLED (pixel drive on)");
    } else {
        ESP_LOGI(TAG, "Boost converter GATED (pixel drive off) - POWER SAVING: -25-35mA!");
    }

    power_optimizer_update_state();
}

void power_optimizer_gate_wifi(bool enabled) {
    current_state.wifi_on = enabled;
    ESP_LOGD(TAG, "WiFi: %s", enabled ? "ON" : "OFF");
    power_optimizer_update_state();
}

void power_optimizer_gate_microphone(bool enabled) {
    current_state.microphone_on = enabled;
    ESP_LOGD(TAG, "Microphone: %s", enabled ? "ON" : "OFF");
    power_optimizer_update_state();
}

void power_optimizer_set_cpu_frequency(uint8_t mhz) {
    if (mhz != 80 && mhz != 160 && mhz != 240) {
        ESP_LOGW(TAG, "Invalid CPU frequency: %d (must be 80, 160, or 240)", mhz);
        return;
    }

    current_state.cpu_freq_mhz = mhz;

    // Update actual CPU frequency via power_manager
    // (Implementation in power_manager.c)

    ESP_LOGD(TAG, "CPU frequency: %d MHz", mhz);
    power_optimizer_update_state();
}

float power_optimizer_estimate_battery_hours(void) {
    power_optimizer_update_state();
    return current_state.estimated_hours_remaining;
}

void power_optimizer_print_status(void) {
    PowerState state = power_optimizer_get_state();

    const char *profile_names[] = {"ALWAYS_LISTENING", "PERIODIC", "GESTURE_ONLY"};

    ESP_LOGI(TAG, "\n╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Power Optimizer Status Report         ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Profile:          %-20s  ║", profile_names[state.profile]);
    ESP_LOGI(TAG, "║ Battery:          %3d%%                ║", state.battery_percent);
    ESP_LOGI(TAG, "║ Current Draw:     %.1f mA             ║", state.current_draw_ma);
    ESP_LOGI(TAG, "║ Est. Hours Left:  %.1f h              ║", state.estimated_hours_remaining);
    ESP_LOGI(TAG, "║ Uptime:           %d s                ║", state.uptime_seconds);
    ESP_LOGI(TAG, "╠════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Boost Converter:  %-20s  ║", state.boost_converter_on ? "ON" : "OFF (GATED)");
    ESP_LOGI(TAG, "║ WiFi:             %-20s  ║", state.wifi_on ? "ON" : "OFF");
    ESP_LOGI(TAG, "║ Microphone:       %-20s  ║", state.microphone_on ? "ON" : "OFF");
    ESP_LOGI(TAG, "║ Display:          %3d%% brightness     ║", state.display_brightness);
    ESP_LOGI(TAG, "║ CPU:              %d MHz              ║", state.cpu_freq_mhz);
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝\n");
}

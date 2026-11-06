#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "voice_loop.h"
#include "microsd_manager.h"
#include "power_manager.h"
#include "audio_driver.h"
#include "network_manager.h"
#include "wake_word_detector.h"
#include "audio_circular_buffer.h"
#include "power_profile_manager.h"
#include "periodic_listener.h"
#include "speaker_identification.h"
#include "speaker_context_manager.h"
#include "ui_core.h"
#include "ui_home.h"
#include "display_driver.h"
#include "power_optimizer.h"
#include "voice_activity_detection.h"
#include "microphone_duty_cycle.h"

static const char *TAG = "BUG_MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Bug Voice Assistant - ESP32-S3 Firmware     ║");
    ESP_LOGI(TAG, "║  Colombian Spanish Voice Companion            ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════╝");

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS initialized");

    // Initialize MicroSD card
    ESP_LOGI(TAG, "Initializing MicroSD card...");
    if (microsd_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ MicroSD initialization failed - continuing without offline models");
    } else {
        ESP_LOGI(TAG, "✓ MicroSD initialized");
        uint64_t free = microsd_get_free_space();
        uint64_t total = microsd_get_total_space();
        ESP_LOGI(TAG, "  Storage: %.2f GB / %.2f GB free", free / 1e9, total / 1e9);
    }

    // Initialize audio driver
    ESP_LOGI(TAG, "Initializing audio driver...");
    if (audio_init(AUDIO_DIR_DUPLEX) != ESP_OK) {
        ESP_LOGE(TAG, "✗ Audio initialization failed");
        esp_restart();
    }
    ESP_LOGI(TAG, "✓ Audio driver ready");

    // Initialize circular audio buffer
    audio_circular_buffer_init();
    ESP_LOGI(TAG, "✓ Circular audio buffer ready");

    // Initialize wake word detector
    wake_word_detector_init();
    ESP_LOGI(TAG, "✓ Wake word detector ready");

    // Initialize power profile manager
    ESP_LOGI(TAG, "Initializing power profiles...");
    if (power_profile_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Power profile manager initialization failed");
    } else {
        ESP_LOGI(TAG, "✓ Power profile manager ready (ALWAYS_LISTENING mode)");
    }

    // Initialize periodic listener
    ESP_LOGI(TAG, "Initializing periodic listener...");
    if (periodic_listener_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Periodic listener initialization failed");
    } else {
        ESP_LOGI(TAG, "✓ Periodic listener ready (can switch to low-power mode)");
    }

    // Initialize speaker identification
    ESP_LOGI(TAG, "Initializing speaker identification...");
    if (speaker_id_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Speaker ID initialization failed");
    } else {
        ESP_LOGI(TAG, "✓ Speaker ID ready (multi-user support enabled)");
        int num_profiles = speaker_id_get_profile_count();
        ESP_LOGI(TAG, "  Enrolled speakers: %d", num_profiles);
        if (num_profiles > 0) {
            speaker_id_print_profiles();
        }
    }

    // Initialize display driver (AMOLED + touch)
    ESP_LOGI(TAG, "Initializing display driver...");
    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "✗ Display initialization failed");
        esp_restart();
    }
    ESP_LOGI(TAG, "✓ Display driver ready");

    // Initialize UI (Littlevgl for 466x466 circular AMOLED display)
    ESP_LOGI(TAG, "Initializing UI system...");
    ui_init();
    ESP_LOGI(TAG, "✓ UI system ready (dark theme, 466x466 circular AMOLED)");

    // Initialize power manager
    ESP_LOGI(TAG, "Initializing power management...");
    if (power_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Power manager initialization failed");
    } else {
        ESP_LOGI(TAG, "✓ Power manager ready (modem sleep enabled)");
    }

    // Initialize network manager (WiFi + 4G fallback)
    ESP_LOGI(TAG, "Initializing network manager...");
    if (network_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Network initialization failed - operating in offline mode");
    } else {
        ESP_LOGI(TAG, "✓ Network manager ready (WiFi + 4G fallback available)");
    }

    // Initialize power optimizer (battery + power profiles)
    ESP_LOGI(TAG, "Initializing power optimizer...");
    if (power_optimizer_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Power optimizer initialization failed");
    } else {
        ESP_LOGI(TAG, "✓ Power optimizer ready (ALWAYS_LISTENING profile)");
    }

    // Initialize voice activity detection (VAD)
    ESP_LOGI(TAG, "Initializing voice activity detection...");
    vad_init();
    ESP_LOGI(TAG, "✓ VAD ready (filters silence)");

    // Initialize microphone duty cycling
    ESP_LOGI(TAG, "Initializing microphone duty cycle...");
    mic_duty_cycle_init(MIC_DUTY_LISTEN_MS, MIC_DUTY_SLEEP_MS);
    ESP_LOGI(TAG, "✓ Mic duty cycle ready (2s listen, 3s sleep in Periodic mode)");

    // Initialize voice loop
    ESP_LOGI(TAG, "Initializing voice loop...");
    voice_loop_init();
    ESP_LOGI(TAG, "✓ Voice loop ready");

    ESP_LOGI(TAG, "\n╔═══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  🎤 Bug is ready to listen!                 ║");
    ESP_LOGI(TAG, "║  Mode: ALWAYS_LISTENING (8-12mA)            ║");
    ESP_LOGI(TAG, "║  Display: Dark theme (#121212, 30-58% power)║");
    ESP_LOGI(TAG, "║  Battery: ~8-14 days per 2000mAh charge     ║");
    ESP_LOGI(TAG, "║  Say \"Hola Bug\" for hands-free activation   ║");
    ESP_LOGI(TAG, "║  Multi-user support: YES (speaker ID)       ║");
    ESP_LOGI(TAG, "║  Processing: STT → Speaker ID → LLM → TTS   ║");
    ESP_LOGI(TAG, "║  Power Profiles: Always, Periodic, Gesture   ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════╝\n");

    // Main event loop
    int capture_count = 0;
    uint32_t idle_start_time = xTaskGetTickCount();
    int last_idle_reported = 0;

    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // Monitor idle time and switch to periodic listening
        uint32_t idle_duration_ms = xTaskGetTickCount() - idle_start_time;
        power_profile_manager_notify_idle(idle_duration_ms);

        // Periodic logging and power monitoring
        if (capture_count++ % 200 == 0) {
            PowerState power_state = power_optimizer_get_state();
            VADState vad_state = vad_get_state();

            ESP_LOGI(TAG, "🔋 Battery: %d%%, Power: %.1f mA, Est. %.1f hours | "
                         "VAD: energy=%.1f, voice=%s | Boost: %s",
                     power_state.battery_percent,
                     power_state.current_draw_ma,
                     power_state.estimated_hours_remaining,
                     vad_state.energy_level,
                     vad_state.voice_detected ? "YES" : "NO",
                     power_state.boost_converter_on ? "ON" : "GATED");

            // Auto-switch power profile based on battery
            if (power_state.battery_percent < 5) {
                power_optimizer_set_profile(PROFILE_GESTURE_ONLY);
            } else if (power_state.battery_percent < 20) {
                power_optimizer_set_profile(PROFILE_PERIODIC);
            }
        }
    }
}

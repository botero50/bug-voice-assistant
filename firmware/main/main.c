#include <stdbool.h>
#include <stdio.h>
#include "lvgl.h"
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
#include "assistant.h"
#include "settings_store.h"
#include "ui_nav.h"
#include "battery.h"
#include "clock.h"
#include "network_manager.h"
#include "wake_word_detector.h"
#include "audio_circular_buffer.h"
#include "power_profile_manager.h"
#include "periodic_listener.h"
#include "speaker_identification.h"
#include "speaker_context_manager.h"
#include "ui_core.h"
#include "ui_home.h"
#include "ui_config.h"
#include "display_driver.h"
#include "power_optimizer.h"
#include "voice_activity_detection.h"
#include "microphone_duty_cycle.h"

static const char *TAG = "BUG_MAIN";

#define STANDBY_PROMPT "Di “Hola Bug”"
#define ANSWER_LINGER_MS 10000  // how long an answer stays on screen after it was spoken

static assistant_state_t previous_state = ASSISTANT_STATE_CONNECTING;
static uint32_t answer_expires_ms = 0;
static bool wifi_screen_offered = false;

static uint32_t now_ms(void) {
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

static void show_texts(const char *heard, const char *answer) {
    char quoted[140];
    if (heard && heard[0]) {
        snprintf(quoted, sizeof(quoted), "“%s”", heard);
    } else {
        quoted[0] = '\0';
    }
    ui_home_set_texts(quoted, answer);
}

static void show_assistant_state(assistant_state_t state) {
    char heard[128], reply[256];
    assistant_get_texts(heard, sizeof(heard), reply, sizeof(reply));
    answer_expires_ms = 0;

    if (state == ASSISTANT_STATE_HEARING || state == ASSISTANT_STATE_LISTENING || state == ASSISTANT_STATE_THINKING ||
        state == ASSISTANT_STATE_SPEAKING) {
        ui_nav_show_home_for_voice();
    }

    switch (state) {
    case ASSISTANT_STATE_NO_CONFIG:
        ui_home_set_mode(UI_HOME_SETUP);
        show_texts("", "Toca el círculo para configurar el WiFi");
        if (!wifi_screen_offered && ui_nav_current() == UI_SCREEN_HOME) {
            wifi_screen_offered = true;  // first time only: take the user straight to the list
            ui_nav_go(UI_SCREEN_WIFI, LV_SCR_LOAD_ANIM_MOVE_LEFT);
        }
        break;
    case ASSISTANT_STATE_WIFI_ERROR:
        ui_home_set_mode(UI_HOME_SETUP);
        show_texts("", network_manager_last_error() == NET_ERR_PASSWORD ? "Contraseña de WiFi incorrecta. Toca el círculo"
                       : network_manager_last_error() == NET_ERR_NOT_FOUND ? "No encuentro tu red WiFi. Toca el círculo"
                                                                           : "No pude conectar al WiFi. Toca el círculo");
        break;
    case ASSISTANT_STATE_MUTED:
        ui_home_set_mode(UI_HOME_MUTED);
        show_texts("", "Micrófono apagado");
        break;
    case ASSISTANT_STATE_CONNECTING:
        ui_home_set_mode(UI_HOME_STANDBY);
        show_texts("", "Conectando al WiFi...");
        break;
    case ASSISTANT_STATE_STANDBY:
        ui_home_set_mode(UI_HOME_STANDBY);
        if (previous_state == ASSISTANT_STATE_SPEAKING) {
            show_texts(heard, reply);  // keep the last exchange visible for a while
            answer_expires_ms = now_ms() + ANSWER_LINGER_MS;
        } else {
            show_texts("", STANDBY_PROMPT);
        }
        break;
    case ASSISTANT_STATE_HEARING:
        ui_home_set_mode(UI_HOME_HEARING);
        show_texts("", "");
        break;
    case ASSISTANT_STATE_LISTENING:
        ui_home_set_mode(UI_HOME_LISTENING);
        show_texts(heard, "Te escucho...");
        break;
    case ASSISTANT_STATE_THINKING:
        ui_home_set_mode(UI_HOME_THINKING);
        show_texts(heard, "Pensando...");
        break;
    case ASSISTANT_STATE_SPEAKING:
        ui_home_set_mode(UI_HOME_SPEAKING);
        show_texts(heard, reply);
        break;
    case ASSISTANT_STATE_NO_SERVER:
        ui_home_set_mode(UI_HOME_ERROR);
        show_texts("", "No encuentro el servidor");
        break;
    }
    previous_state = state;
}

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
    settings_load();
    bug_settings_t settings;
    settings_copy(&settings);

    // Initialize the display first: its buffers need DMA-capable internal RAM,
    // which later startup steps use up.
    ESP_LOGI(TAG, "Initializing display driver...");
    bool display_ready = display_init() == ESP_OK;
    if (display_ready) {
        ESP_LOGI(TAG, "✓ Display driver ready");

        // Initialize UI (LVGL for 466x466 circular AMOLED display)
        ESP_LOGI(TAG, "Initializing UI system...");
        ui_init();
        display_set_brightness(settings.brightness);
        lv_timer_handler();
        ESP_LOGI(TAG, "✓ UI system ready (dark theme, 466x466 circular AMOLED)");
    } else {
        ESP_LOGE(TAG, "✗ Display initialization failed - continuing without screen");
    }

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
    bool audio_ready = audio_init(AUDIO_DIR_DUPLEX) == ESP_OK;
    if (!audio_ready) {
        ESP_LOGE(TAG, "✗ Audio initialization failed - continuing without audio");
    } else {
        audio_set_volume(settings.volume);
        ESP_LOGI(TAG, "✓ Audio driver ready");
    }

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

    ESP_LOGI(TAG, "Bug preview firmware started");

    if (audio_ready) {
        assistant_start();
    }

    // Main event loop. Everything that touches the screen happens here, because
    // LVGL isn't thread-safe.
    uint32_t last_level_ms = 0;
    uint32_t last_clock_ms = 0;
    uint32_t last_battery_ms = 0;
    int last_battery_percent = -2;

    while (1) {
        uint32_t now = now_ms();

        if (display_ready) {
            assistant_state_t state;
            if (assistant_take_state(&state)) {
                show_assistant_state(state);
            }

            if (now - last_level_ms >= 50) {
                last_level_ms = now;
                ui_home_push_level(assistant_audio_level());
            }

            if (now - last_clock_ms >= 1000) {
                last_clock_ms = now;
                int hour, minute;
                if (clock_get(&hour, &minute)) {
                    ui_update_time(hour, minute);
                } else {
                    ui_update_time(99, 0);
                }
                ui_update_wifi_status(network_manager_is_connected() == ESP_OK);

                if (answer_expires_ms && now > answer_expires_ms) {
                    answer_expires_ms = 0;
                    show_texts("", STANDBY_PROMPT);
                }
            }

            if (last_battery_ms == 0 || now - last_battery_ms >= 5000) {
                last_battery_ms = now;
                battery_status_t battery;
                if (battery_read(&battery)) {
                    ui_update_power(battery.percent, battery.charging);
                    if (battery.percent != last_battery_percent) {
                        last_battery_percent = battery.percent;
                        ESP_LOGI(TAG, "Battery %d%%, charging=%d, USB power=%d", battery.percent, battery.charging,
                                 battery.usb_power);
                    }
                }
            }

            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

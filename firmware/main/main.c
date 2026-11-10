#include "mem_report.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#include "alarms.h"
#include "assistant.h"
#include "dev_bridge.h"
void config_serial_start(void);
#include "music_player.h"
#include "ota_update.h"
#include "esp_heap_caps.h"
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
#define DIM_AFTER_MS 10000      // no touch and no voice for this long: dim the screen (battery, AMOLED burn-in)
#define OFF_AFTER_MS 20000      // and switch it off after this long; touch, voice or an alarm wakes it
#define DIM_DIVISOR 6           // dimmed brightness is the chosen brightness divided by this
#define ANSWER_LINGER_MS 10000  // how long an answer stays on screen after it was spoken

static assistant_state_t previous_state = ASSISTANT_STATE_CONNECTING;
static uint32_t answer_expires_ms = 0;
static bool wifi_screen_offered = false;
static bool standby_prompt_active = false;  // Home shows "Di Hola Bug" (plus the next alarm)
static char standby_alarm_line[72] = "";

static uint32_t now_ms(void) {
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

// "Di Hola Bug", with the next alarm or timer above it and how long until it rings.
static void refresh_standby_prompt(bool force) {
    char line[72] = "";
    alarm_entry_t next;
    if (alarms_next(&next)) {
        long left = (long)(next.at - time(NULL));
        char remaining[24];
        if (left >= 3600) {
            snprintf(remaining, sizeof(remaining), "%ld h %02ld min", left / 3600, left % 3600 / 60);
        } else if (left >= 60) {
            snprintf(remaining, sizeof(remaining), "%ld min", (left + 59) / 60);
        } else {
            snprintf(remaining, sizeof(remaining), "%ld s", left < 0 ? 0 : left);
        }
        snprintf(line, sizeof(line), LV_SYMBOL_BELL "  %.22s · %s", next.label, remaining);
    }
    if (!force && strcmp(line, standby_alarm_line) == 0) {
        return;
    }
    strlcpy(standby_alarm_line, line, sizeof(standby_alarm_line));
    ui_home_set_texts(line, STANDBY_PROMPT);
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
    standby_prompt_active = false;

    // While the user talks to Bug, a banner replaces the clock on any screen, so a skill
    // screen stays visible instead of being replaced by the listening ring.
    switch (state) {
    case ASSISTANT_STATE_HEARING:
        ui_voice_banner("Escuchando", COLOR_ACCENT_GREEN);
        break;
    case ASSISTANT_STATE_LISTENING:
        ui_voice_banner("Te escucho", COLOR_ACCENT_GREEN);
        break;
    case ASSISTANT_STATE_THINKING:
        ui_voice_banner("Pensando", COLOR_ACCENT_BLUE);
        break;
    case ASSISTANT_STATE_SPEAKING:
        ui_voice_banner("Hablando", COLOR_ACCENT_WHITE);
        break;
    default:
        ui_voice_banner(NULL, COLOR_ACCENT_WHITE);
        break;
    }
    if (state == ASSISTANT_STATE_HEARING || state == ASSISTANT_STATE_LISTENING || state == ASSISTANT_STATE_THINKING ||
        state == ASSISTANT_STATE_SPEAKING) {
        ui_nav_keep_alive();
    }
    // An answer with no screen of its own is shown on Home
    if (state == ASSISTANT_STATE_SPEAKING && !assistant_screen_pending()) {
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
    case ASSISTANT_STATE_MUSIC: {
        ui_home_set_mode(UI_HOME_MUSIC);
        char playing[120];
        snprintf(playing, sizeof(playing), "%s", music_label());
        ui_home_set_texts("Reproduciendo · toca el círculo para hablar", playing);  // no quotes: it isn't speech
        break;
    }
    case ASSISTANT_STATE_CONNECTING:
        ui_home_set_mode(UI_HOME_STANDBY);
        show_texts("", "Conectando al WiFi...");
        break;
    case ASSISTANT_STATE_STANDBY:
        ui_home_set_mode(UI_HOME_STANDBY);
        // As soon as the answer has been spoken the main screen comes back (the exchange stays in the history)
        standby_prompt_active = true;
        refresh_standby_prompt(true);
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
        show_texts("", "No encuentro el servidor (PC apagado?). Aún hago lo básico sin él");
        break;
    case ASSISTANT_STATE_MIC_ERROR:
        ui_home_set_mode(UI_HOME_ERROR);
        show_texts("", "No oigo el micrófono. Reinicia la placa");
        break;
    }
    previous_state = state;
}

void app_main(void) {
    mem_task_note("main", 8192);
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
    alarms_init();
    alarms_start_ringer();
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

    clock_restore();

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
    power_manager_ui_boost(true);
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
        config_serial_start();  // lets the installer page set the board up over USB
        dev_bridge_start();     // messages from the development PC
        assistant_start();
    }

    // Main event loop. Everything that touches the screen happens here, because
    // LVGL isn't thread-safe.
    uint32_t last_level_ms = 0;
    uint32_t last_clock_ms = 0;
    uint32_t last_battery_ms = 0;
    int last_battery_percent = -2;
    int battery_warned = 100;

    uint32_t last_alarm_ms = 0;
    bool started_ok_marked = false;
    uint32_t voice_activity_ms = 0;
    int screen_level = 0;  // 0 on, 1 dimmed, 2 off
    bool voice_busy = false;  // Bug is hearing, thinking or speaking: keep the screen on

    vTaskPrioritySet(NULL, 8);  // this loop draws the screen: it must not wait behind the background work
    while (1) {
        uint32_t now = now_ms();

        // A freshly updated firmware that runs this long is good: keep it (otherwise the old one returns)
        if (!started_ok_marked && now > 20000) {
            started_ok_marked = true;
            ota_mark_started_ok();
        }

        // Timers and alarms ring even without a screen, and even if the PC is off
        if (now - last_alarm_ms >= 1000) {
            last_alarm_ms = now;
            alarm_entry_t due;
            if (alarms_pop_due(&due)) {
                ESP_LOGI(TAG, "Ringing: %s", due.label);
                alarm_ring_start(due.label);
                if (display_ready) {
                    ui_nav_open_alarm(due.label);
                }
            }
        }

        if (display_ready) {
            assistant_state_t state;
            if (assistant_take_state(&state)) {
                show_assistant_state(state);
                voice_activity_ms = now;  // any change is activity, e.g. Bug just finished an answer
                voice_busy = state == ASSISTANT_STATE_HEARING || state == ASSISTANT_STATE_LISTENING ||
                             state == ASSISTANT_STATE_THINKING || state == ASSISTANT_STATE_SPEAKING;
            }

            // A voice command can open a screen: "muéstrame los correos" opens the email screen
            char screen_path[48];
            char *screen_json = NULL;
            if (assistant_take_screen(screen_path, sizeof(screen_path), &screen_json)) {
                ESP_LOGI(TAG, "Opening screen %s", screen_path);
                voice_activity_ms = now;
                ui_nav_open_screen(screen_path, screen_json);
                heap_caps_free(screen_json);
            }

            {
                char dev_text[300];
                int dev_level;
                bool dev_speak;
                int dev_ask;
                ui_approval_sync(dev_bridge_pending_ask());
                ui_choices_sync(dev_bridge_choice_open());
                {
                    const uint8_t *shot_pixels;
                    int shot_w, shot_h;
                    if (dev_bridge_shot_take(&shot_pixels, &shot_w, &shot_h)) {
                        ui_shot_show(shot_pixels, shot_w, shot_h);
                        voice_activity_ms = now;  // wakes the screen
                    }
                }
                if (dev_bridge_take(dev_text, sizeof(dev_text), &dev_level, &dev_speak, &dev_ask)) {
                    lv_color_t color = dev_level == 1 ? COLOR_ACCENT_GREEN : dev_level == 2 ? lv_color_make(255, 170, 0) : dev_level == 3 ? COLOR_ACCENT_RED : COLOR_ACCENT_BLUE;
                    char choices[4][120];
                    int choice_count = dev_bridge_last_choices(choices);
                    const char *full_text = dev_bridge_last_full();
                    if (dev_ask > 0) {
                        ui_approval_show(dev_text, dev_ask);
                    } else if (choice_count >= 2) {
                        ui_choices_show(dev_text, (const char (*)[120])choices, choice_count, color);
                    } else if (full_text[0]) {
                        ui_choices_show(full_text, NULL, 0, color);  // the whole explanation, to read and scroll
                    } else {
                        ui_notice(dev_text, color, 7000);
                    }
                    voice_activity_ms = now;  // wakes the screen
                    if (dev_speak) assistant_announce(dev_text);
                }
            }

            char ota_message[64];
            if (ota_take_message(ota_message, sizeof(ota_message))) {
                ui_notice(ota_message, COLOR_ACCENT_BLUE, 6000);
            }

            if (now - last_level_ms >= 50) {
                last_level_ms = now;
                {
                    float mic = assistant_audio_level(), out = audio_output_level();
                    ui_home_push_level(mic > out ? mic : out);
                }
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

                // Dim, then switch the screen off, when nobody touches or talks to Bug; touch, voice or an
                // alarm brings it back. A touch that wakes the screen is not also a tap on a button.
                uint32_t idle = lv_disp_get_inactive_time(NULL);
                if (now - voice_activity_ms < idle) {
                    idle = now - voice_activity_ms;
                }
                if (alarm_ringing()) {
                    idle = 0;
                }
                if (voice_busy) {
                    voice_activity_ms = now;
                    idle = 0;
                }
                int wanted_level = idle > OFF_AFTER_MS ? 2 : (idle > DIM_AFTER_MS ? 1 : 0);
                if (wanted_level != screen_level && (wanted_level > screen_level || idle < 1500)) {
                    bug_settings_t now_settings;
                    settings_copy(&now_settings);
                    if (wanted_level == 2) {
                        display_gate_boost_converter(false);  // display off: no light at all
                    } else {
                        if (screen_level == 2) {
                            display_gate_boost_converter(true);
                            ui_nav_swallow_touch();
                        }
                        uint8_t level = now_settings.brightness / DIM_DIVISOR;
                        display_set_brightness(wanted_level == 1 ? (level < 3 ? 3 : level) : now_settings.brightness);
                    }
                    screen_level = wanted_level;
                    power_manager_ui_boost(wanted_level == 0);  // full speed only while the screen is fully on
                }
                ui_nav_tick();

                if (answer_expires_ms && now > answer_expires_ms) {
                    answer_expires_ms = 0;
                    standby_prompt_active = true;
                    refresh_standby_prompt(true);
                } else if (standby_prompt_active) {
                    refresh_standby_prompt(false);  // keeps the alarm countdown current
                }

                if (music_sleep_expired()) {
                    ESP_LOGI(TAG, "Sleep timer: turning the music off");
                    music_stop();
                    if (ui_nav_current() == UI_SCREEN_MUSIC) {
                        ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP);
                    }
                }
            }

            if (last_battery_ms == 0 || now - last_battery_ms >= 5000) {
                last_battery_ms = now;
                battery_status_t battery;
                if (battery_read(&battery)) {
                    ui_update_power(battery.percent, battery.charging);
                    // Warn once when the level drops through 15% and again through 5%; re-armed by charging
                    if (battery.charging || battery.percent > 20) {
                        battery_warned = 100;
                    } else if (battery.percent <= 5 && battery_warned > 5) {
                        battery_warned = 5;
                        ui_notice("Batería casi vacía", COLOR_ACCENT_RED, 8000);
                    } else if (battery.percent <= 15 && battery_warned > 15) {
                        battery_warned = 15;
                        ui_notice("Batería baja · conecta el cargador", COLOR_ACCENT_RED, 6000);
                    }
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

#include "assistant.h"
#include "alarms.h"
#include "audio_driver.h"
#include "clock.h"
#include "music_player.h"
#include "network_manager.h"
#include "ota_update.h"
#include "settings_store.h"
#include "stopwatch.h"
#include "voice_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ASSISTANT";

#define SAMPLE_RATE 16000
#define FRAME_SAMPLES 320                          // 20 ms
#define FRAME_MS 20
#define PREROLL_FRAMES 15                          // keep 300 ms from before speech starts
#define MAX_UTTERANCE_MS 8000
#define MAX_UTTERANCE_SAMPLES (SAMPLE_RATE / 1000 * MAX_UTTERANCE_MS)
#define END_SILENCE_MS 800                         // a pause this long ends the sentence
#define KEEP_SILENCE_MS 300
#define MIN_SPEECH_FRAMES 8                        // ignore clicks and coughs shorter than 160 ms
#define COMMAND_WAIT_MS 6000                       // after "Hola Bug", wait this long for the command
#define MIN_THRESHOLD 150.0f                       // never trigger below this level
#define NOISE_MULTIPLIER 3.0f                      // speech starts this far above the room noise
#define LOG_INTERVAL_MS 10000

static volatile assistant_state_t current_state = ASSISTANT_STATE_CONNECTING;
static volatile bool state_changed = true;
static volatile float audio_level = 0;
static volatile bool wifi_dirty = false;
static volatile bool listen_now_request = false;
static volatile bool muted = false;
static float noise_floor = 80.0f;
static char heard_text[128];
static char reply_text[256];

typedef struct {
    char heard[128];
    char reply[256];
} exchange_t;
static exchange_t history[ASSISTANT_HISTORY_SIZE];
static int history_count = 0;

static void set_state(assistant_state_t state) {
    if (state == current_state && state_changed) {
        return;
    }
    current_state = state;
    state_changed = true;
}

bool assistant_take_state(assistant_state_t *state) {
    if (!state_changed) {
        return false;
    }
    state_changed = false;
    *state = current_state;
    return true;
}

float assistant_audio_level(void) {
    return audio_level;
}

void assistant_get_texts(char *heard, size_t heard_size, char *reply, size_t reply_size) {
    strlcpy(heard, heard_text, heard_size);
    strlcpy(reply, reply_text, reply_size);
}

static char screen_path[48];
static char *screen_json = NULL;
static volatile bool screen_ready = false;

bool assistant_screen_pending(void) {
    return screen_ready;
}

bool assistant_take_screen(char *path, size_t path_size, char **json) {
    if (!screen_ready) {
        return false;
    }
    strlcpy(path, screen_path, path_size);
    *json = screen_json;
    screen_json = NULL;
    screen_ready = false;
    return true;
}

static void publish_screen(const char *path, char *json) {
    heap_caps_free(screen_json);  // an older screen nobody picked up
    strlcpy(screen_path, path, sizeof(screen_path));
    screen_json = json;
    screen_ready = true;
}

void assistant_wifi_changed(void) {
    wifi_dirty = true;
}

void assistant_listen_now(void) {
    if (current_state == ASSISTANT_STATE_STANDBY || current_state == ASSISTANT_STATE_MUSIC) {
        listen_now_request = true;
    }
}

void assistant_set_muted(bool value) {
    muted = value;
}

bool assistant_history_get(int index, char *heard, size_t heard_size, char *reply, size_t reply_size) {
    if (index < 0 || index >= history_count) {
        return false;
    }
    strlcpy(heard, history[index].heard, heard_size);
    strlcpy(reply, history[index].reply, reply_size);
    return true;
}

static void remember_exchange(const char *heard, const char *reply) {
    memmove(&history[1], &history[0], sizeof(exchange_t) * (ASSISTANT_HISTORY_SIZE - 1));
    strlcpy(history[0].heard, heard, sizeof(history[0].heard));
    strlcpy(history[0].reply, reply, sizeof(history[0].reply));
    if (history_count < ASSISTANT_HISTORY_SIZE) history_count++;
}

static void set_texts(const char *heard, const char *reply) {
    strlcpy(heard_text, heard, sizeof(heard_text));
    strlcpy(reply_text, reply, sizeof(reply_text));
}

static float frame_rms(const int16_t *frame) {
    double sum = 0;
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        sum += (double)frame[i] * frame[i];
    }
    return sqrtf((float)(sum / FRAME_SAMPLES));
}

// 0 at the room's quiet level, 1 for loud speech (about 30 dB louder).
static void publish_level(float rms) {
    float level = 20.0f * log10f(fmaxf(rms, 1.0f) / 100.0f) / 30.0f;
    audio_level = level < 0 ? 0 : (level > 1 ? 1 : level);
}

#define MIC_FAULT_AFTER_FAILURES 300  // about three seconds of reads that return nothing

static bool read_frame(int16_t *frame) {
    static int failures = 0;
    size_t got = 0;
    esp_err_t ret = audio_read_samples(frame, FRAME_SAMPLES * sizeof(int16_t), &got);
    if (ret != ESP_OK || got != FRAME_SAMPLES * sizeof(int16_t)) {
        if (++failures == MIC_FAULT_AFTER_FAILURES) {
            ESP_LOGE(TAG, "The microphone is not delivering audio");
            set_state(ASSISTANT_STATE_MIC_ERROR);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        return false;
    }
    if (failures >= MIC_FAULT_AFTER_FAILURES) {
        ESP_LOGI(TAG, "The microphone is back");
        set_state(ASSISTANT_STATE_STANDBY);
    }
    failures = 0;
    return true;
}

// The PC did not answer. If the Wi-Fi itself dropped, wait for it to come back (the network
// manager keeps retrying); otherwise show that the server is missing.
static void report_unreachable(void) {
    set_texts("", "");
    if (network_manager_is_connected() != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi lost, waiting for it to come back");
        set_state(ASSISTANT_STATE_CONNECTING);
        for (int i = 0; i < 60 && network_manager_is_connected() != ESP_OK && !wifi_dirty; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        return;
    }
    set_state(ASSISTANT_STATE_NO_SERVER);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static void track_noise(float rms) {
    // Falls quickly when the room gets quieter, rises slowly so speech doesn't raise it.
    noise_floor = rms < noise_floor ? 0.9f * noise_floor + 0.1f * rms : 0.998f * noise_floor + 0.002f * rms;
}

// Waits for someone to speak, then records until they pause. Returns the number of
// samples, or 0 if nothing was said within wait_ms (0 means wait forever), or if
// `interruptible` and a tap or the mute switch asked for something else.
// speech_state is shown on screen as soon as speech starts.
static size_t capture_utterance(int16_t *out, uint32_t wait_ms, assistant_state_t speech_state, bool interruptible) {
    static int16_t preroll[PREROLL_FRAMES][FRAME_SAMPLES];
    int16_t frame[FRAME_SAMPLES];
    size_t head = 0;
    size_t filled = 0;
    uint32_t waited_ms = 0;
    uint32_t log_ms = 0;
    float threshold = 0;
    bool muted_at_start = muted;
    bool music_at_start = music_is_playing();

    audio_level = 0;
    for (;;) {
        if (!read_frame(frame)) {
            continue;
        }
        float rms = frame_rms(frame);
        threshold = fmaxf(noise_floor * NOISE_MULTIPLIER, MIN_THRESHOLD);

        if (interruptible && (listen_now_request || muted != muted_at_start || music_is_playing() != music_at_start)) {
            return 0;
        }

        memcpy(preroll[head], frame, sizeof(frame));
        head = (head + 1) % PREROLL_FRAMES;
        if (filled < PREROLL_FRAMES) filled++;

        // While music plays the mic would hear the music itself, so only a tap on the ring starts a command
        if (rms > threshold && !muted && !music_is_playing() && !alarm_ringing()) {
            ESP_LOGI(TAG, "Speech started: level %.0f, trigger %.0f, room noise %.0f", rms, threshold, noise_floor);
            publish_level(rms);
            set_state(speech_state);
            break;
        }
        track_noise(rms);

        waited_ms += FRAME_MS;
        log_ms += FRAME_MS;
        if (log_ms >= LOG_INTERVAL_MS) {
            log_ms = 0;
            ESP_LOGI(TAG, "Quiet: room noise %.0f, trigger %.0f", noise_floor, threshold);
        }
        if (wait_ms && waited_ms >= wait_ms) {
            return 0;
        }
    }

    size_t samples = 0;
    size_t first = (head + PREROLL_FRAMES - filled) % PREROLL_FRAMES;
    for (size_t i = 0; i < filled; i++) {
        memcpy(out + samples, preroll[(first + i) % PREROLL_FRAMES], sizeof(frame));
        samples += FRAME_SAMPLES;
    }

    uint32_t silent_ms = 0;
    int loud_frames = 0;
    int peak = 0;
    while (samples + FRAME_SAMPLES <= MAX_UTTERANCE_SAMPLES && silent_ms < END_SILENCE_MS) {
        if (!read_frame(frame)) {
            continue;
        }
        memcpy(out + samples, frame, sizeof(frame));
        samples += FRAME_SAMPLES;

        float rms = frame_rms(frame);
        publish_level(rms);
        if (rms > threshold * 0.7f) {
            silent_ms = 0;
            loud_frames++;
        } else {
            silent_ms += FRAME_MS;
        }
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            int level = frame[i] < 0 ? -frame[i] : frame[i];
            if (level > peak) peak = level;
        }
    }
    audio_level = 0;

    if (loud_frames < MIN_SPEECH_FRAMES) {
        ESP_LOGI(TAG, "Too short to be speech (%d loud frames), ignoring", loud_frames);
        return 0;
    }
    if (silent_ms > KEEP_SILENCE_MS) {
        samples -= (silent_ms - KEEP_SILENCE_MS) / 1000.0f * SAMPLE_RATE;
    }
    ESP_LOGI(TAG, "Recorded %.1f s, peak %d", (float)samples / SAMPLE_RATE, peak);
    return samples;
}

static void chime(void) {
    audio_set_speaker(true);
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_beep(880.0f, 90);
    audio_beep(1175.0f, 120);
    vTaskDelay(pdMS_TO_TICKS(60));
    audio_set_speaker(false);
    audio_flush_input(400);
}

// Reads one value out of "timer?id=3&at=173000&l=Llamar%20a%20Ana", decoding the percent signs.
static bool param(const char *path, const char *key, char *out, size_t size) {
    const char *query = strchr(path, '?');
    if (!query) {
        return false;
    }
    size_t key_length = strlen(key);
    for (const char *p = query + 1; *p;) {
        if (strncmp(p, key, key_length) == 0 && p[key_length] == '=') {
            const char *value = p + key_length + 1;
            const char *end = strchr(value, '&');
            size_t length = end ? (size_t)(end - value) : strlen(value);
            char raw[160];
            if (length >= sizeof(raw)) length = sizeof(raw) - 1;
            memcpy(raw, value, length);
            raw[length] = '\0';
            voice_client_percent_decode(out, size, raw);
            return true;
        }
        const char *next = strchr(p, '&');
        if (!next) break;
        p = next + 1;
    }
    return false;
}

static void change_volume(int delta);

static void set_volume_to(int percent) {
    bug_settings_t settings;
    settings_copy(&settings);
    change_volume(percent - (int)settings.volume);
}

static void change_volume(int delta) {
    bug_settings_t settings;
    settings_copy(&settings);
    int volume = settings.volume + delta;
    volume = volume < 5 ? 5 : (volume > 100 ? 100 : volume);
    settings_set_volume(volume);
    audio_set_volume(volume);
    ESP_LOGI(TAG, "Volume %d%%", volume);
}

static void speak(const voice_result_t *result) {
    set_texts(result->heard, result->reply);

    // Some answers carry an action instead of, or as well as, a screen
    int volume_delta = 0;
    int volume_set = -1;
    bool start_music = false;
    bool start_ota = false;
    char music_path[160] = "";
    if (result->screen[0]) {
        char *json = NULL;
        if (strncmp(result->screen, "volume?d=", 9) == 0) {
            volume_delta = atoi(result->screen + 9);
        } else if (strncmp(result->screen, "stopwatch?cmd=", 14) == 0) {
            stopwatch_command(result->screen + 14);
            publish_screen("stopwatch", NULL);
        } else if (strncmp(result->screen, "ota?", 4) == 0) {
            start_ota = true;  // after the announcement
        } else if (strncmp(result->screen, "triqui?", 7) == 0) {
            publish_screen("triqui", NULL);
        } else if (strncmp(result->screen, "music_sleep?s=", 14) == 0) {
            music_sleep_in(atoi(result->screen + 14));
        } else if (strncmp(result->screen, "volume?v=", 9) == 0) {
            volume_set = atoi(result->screen + 9);
        } else if (strncmp(result->screen, "timer?", 6) == 0) {
            // Saved on the board first, so it rings even if the PC is off later
            char id[16] = "", at[24] = "", name[64] = "", repeat[16] = "";
            if (param(result->screen, "id", id, sizeof(id)) && param(result->screen, "at", at, sizeof(at))) {
                param(result->screen, "l", name, sizeof(name));
                param(result->screen, "r", repeat, sizeof(repeat));
                alarms_add((uint32_t)strtoul(id, NULL, 10), (time_t)atoll(at), name[0] ? name : "Temporizador",
                           (uint32_t)strtoul(repeat, NULL, 10));
            }
        } else if (strncmp(result->screen, "timer_cancel?", 13) == 0) {
            char ids[96] = "";
            if (param(result->screen, "ids", ids, sizeof(ids))) {
                alarms_cancel_ids(ids);
            }
        } else if (strcmp(result->screen, "music_stop") == 0) {
            music_stop();
            publish_screen("home", NULL);
        } else if (strcmp(result->screen, "home") == 0) {
            publish_screen("home", NULL);
        } else if (voice_client_fetch_screen(result->screen, &json) == ESP_OK) {
            // The screen goes first so it is already coming up when the voice starts.
            publish_screen(result->screen, json);
            if (strncmp(result->screen, "music?", 6) == 0) {
                snprintf(music_path, sizeof(music_path), "music/stream?%s", result->screen + 6);
                start_music = true;
            }
        }
    }

    set_state(ASSISTANT_STATE_SPEAKING);
    audio_set_speaker(true);
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_write_samples(result->audio, result->audio_bytes);
    vTaskDelay(pdMS_TO_TICKS(150));
    audio_set_speaker(false);
    audio_flush_input(500);  // don't hear our own voice

    if (volume_set >= 0) {
        set_volume_to(volume_set);
    }
    if (volume_delta) {
        change_volume(volume_delta);
    }
    if (start_ota) {
        ota_update_start();
    }
    if (start_music) {
        music_start(music_path);  // after the announcement, so the two never overlap
    }
}

static void calibrate_noise(void) {
    int16_t frame[FRAME_SAMPLES];
    int frames = 0;
    while (frames < 50) {  // one second
        if (!read_frame(frame)) {
            continue;
        }
        track_noise(frame_rms(frame));
        frames++;
    }
    ESP_LOGI(TAG, "Room noise level: %.0f", noise_floor);
}

// Keeps trying until the saved network is joined. Returns early to start over
// whenever the Wi-Fi settings change.
static void wait_for_wifi(void) {
    for (;;) {
        bug_settings_t settings;
        settings_copy(&settings);

        if (!settings.wifi_ssid[0]) {
            ESP_LOGI(TAG, "No Wi-Fi saved yet");
            set_state(ASSISTANT_STATE_NO_CONFIG);
            wifi_dirty = false;
            while (!wifi_dirty) {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            continue;
        }

        wifi_dirty = false;
        set_state(ASSISTANT_STATE_CONNECTING);
        esp_err_t err = network_manager_connect_wifi(settings.wifi_ssid, settings.wifi_password);
        if (err != ESP_OK && network_manager_last_error() != NET_ERR_NONE) {
            set_state(ASSISTANT_STATE_WIFI_ERROR);
        }
        while (network_manager_is_connected() != ESP_OK && !wifi_dirty) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (wifi_dirty) {
            continue;
        }
        return;
    }
}

// The wake word was heard (or the ring was tapped): listen for the command and answer it.
static void handle_command_with_voice(int16_t *utterance);

static void handle_command(int16_t *utterance) {
    // Music and Bug's voice share the speaker, so the music waits and then carries on
    music_pause_for_voice();
    handle_command_with_voice(utterance);
    music_resume_after_voice();
}

static void handle_command_with_voice(int16_t *utterance) {
    chime();
    set_state(ASSISTANT_STATE_LISTENING);
    size_t samples = capture_utterance(utterance, COMMAND_WAIT_MS, ASSISTANT_STATE_LISTENING, false);
    if (samples == 0) {
        ESP_LOGI(TAG, "No command heard, back to standby");
        return;
    }

    set_state(ASSISTANT_STATE_THINKING);
    voice_result_t result;
    esp_err_t err = voice_client_listen(true, utterance, samples * sizeof(int16_t), &result);
    if (err != ESP_OK) {
        report_unreachable();
        return;
    }
    if (result.action == VOICE_ACTION_REPLY) {
        ESP_LOGI(TAG, "Heard \"%s\", answering \"%s\"", result.heard, result.reply);
        remember_exchange(result.heard, result.reply);
        speak(&result);
    }
    voice_result_free(&result);
}

static void assistant_task(void *arg) {
    int16_t *utterance = heap_caps_malloc(MAX_UTTERANCE_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!utterance) {
        ESP_LOGE(TAG, "No memory for the recording buffer");
        vTaskDelete(NULL);
        return;
    }

    bug_settings_t settings;
    settings_copy(&settings);
    muted = settings.mic_muted;

    wait_for_wifi();
    ESP_LOGI(TAG, "Wi-Fi connected. Server: %s", settings.server_url);
    clock_start();
    calibrate_noise();

    for (;;) {
        set_state(muted ? ASSISTANT_STATE_MUTED : (music_is_playing() ? ASSISTANT_STATE_MUSIC : ASSISTANT_STATE_STANDBY));
        size_t samples = capture_utterance(utterance, 0, ASSISTANT_STATE_HEARING, true);

        if (listen_now_request) {
            listen_now_request = false;
            if (!muted) {
                set_texts("", "");
                handle_command(utterance);
            }
            continue;
        }
        if (samples == 0) {
            continue;
        }

        set_state(ASSISTANT_STATE_THINKING);
        voice_result_t result;
        esp_err_t err = voice_client_listen(false, utterance, samples * sizeof(int16_t), &result);
        if (err != ESP_OK) {
            report_unreachable();
            continue;
        }

        if (result.action == VOICE_ACTION_WAKE) {
            ESP_LOGI(TAG, "Wake word heard (%s), waiting for the command", result.heard);
            set_texts(result.heard, "");
            voice_result_free(&result);
            handle_command(utterance);
        } else if (result.action == VOICE_ACTION_REPLY) {
            ESP_LOGI(TAG, "Heard \"%s\", answering \"%s\"", result.heard, result.reply);
            remember_exchange(result.heard, result.reply);
            speak(&result);
            voice_result_free(&result);
        } else {
            set_texts("", "");
            voice_result_free(&result);
        }
    }
}

void assistant_start(void) {
    xTaskCreatePinnedToCore(assistant_task, "assistant", 8192, NULL, 5, NULL, 1);
}

#include "assistant.h"
#include "audio_driver.h"
#include "bug_config.h"
#include "network_manager.h"
#include "voice_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
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
static float noise_floor = 80.0f;

static void set_state(assistant_state_t state) {
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

static float frame_rms(const int16_t *frame) {
    double sum = 0;
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        sum += (double)frame[i] * frame[i];
    }
    return sqrtf((float)(sum / FRAME_SAMPLES));
}

static bool read_frame(int16_t *frame) {
    size_t got = 0;
    esp_err_t ret = audio_read_samples(frame, FRAME_SAMPLES * sizeof(int16_t), &got);
    if (ret != ESP_OK || got != FRAME_SAMPLES * sizeof(int16_t)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        return false;
    }
    return true;
}

static void track_noise(float rms) {
    // Falls quickly when the room gets quieter, rises slowly so speech doesn't raise it.
    noise_floor = rms < noise_floor ? 0.9f * noise_floor + 0.1f * rms : 0.998f * noise_floor + 0.002f * rms;
}

// Waits for someone to speak, then records until they pause. Returns the number of
// samples, or 0 if nothing was said within wait_ms (0 means wait forever).
static size_t capture_utterance(int16_t *out, uint32_t wait_ms) {
    static int16_t preroll[PREROLL_FRAMES][FRAME_SAMPLES];
    int16_t frame[FRAME_SAMPLES];
    size_t head = 0;
    size_t filled = 0;
    uint32_t waited_ms = 0;
    uint32_t log_ms = 0;
    float threshold = 0;

    for (;;) {
        if (!read_frame(frame)) {
            continue;
        }
        float rms = frame_rms(frame);
        threshold = fmaxf(noise_floor * NOISE_MULTIPLIER, MIN_THRESHOLD);

        memcpy(preroll[head], frame, sizeof(frame));
        head = (head + 1) % PREROLL_FRAMES;
        if (filled < PREROLL_FRAMES) filled++;

        if (rms > threshold) {
            ESP_LOGI(TAG, "Speech started: level %.0f, trigger %.0f, room noise %.0f", rms, threshold, noise_floor);
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

static void speak(const voice_result_t *result) {
    set_state(ASSISTANT_STATE_SPEAKING);
    audio_set_speaker(true);
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_write_samples(result->audio, result->audio_bytes);
    vTaskDelay(pdMS_TO_TICKS(150));
    audio_set_speaker(false);
    audio_flush_input(500);  // don't hear our own voice
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

static void assistant_task(void *arg) {
    int16_t *utterance = heap_caps_malloc(MAX_UTTERANCE_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!utterance) {
        ESP_LOGE(TAG, "No memory for the recording buffer");
        vTaskDelete(NULL);
        return;
    }

    if (strlen(BUG_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "No Wi-Fi name set. Fill in bug_config.local.h and flash again.");
        set_state(ASSISTANT_STATE_NO_CONFIG);
        vTaskDelete(NULL);
        return;
    }

    set_state(ASSISTANT_STATE_CONNECTING);
    network_manager_connect_wifi(BUG_WIFI_SSID, BUG_WIFI_PASSWORD);
    while (network_manager_is_connected() != ESP_OK) {
        ESP_LOGI(TAG, "Waiting for Wi-Fi...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "Wi-Fi connected. Server: %s", BUG_SERVER_URL);

    calibrate_noise();

    for (;;) {
        set_state(ASSISTANT_STATE_STANDBY);
        size_t samples = capture_utterance(utterance, 0);
        if (samples == 0) {
            continue;
        }

        set_state(ASSISTANT_STATE_THINKING);
        voice_result_t result;
        esp_err_t err = voice_client_listen(false, utterance, samples * sizeof(int16_t), &result);

        if (err == ESP_OK && result.action == VOICE_ACTION_WAKE) {
            ESP_LOGI(TAG, "Wake word heard, waiting for the command");
            voice_result_free(&result);
            chime();

            set_state(ASSISTANT_STATE_LISTENING);
            samples = capture_utterance(utterance, COMMAND_WAIT_MS);
            if (samples == 0) {
                ESP_LOGI(TAG, "No command heard, back to standby");
                continue;
            }
            set_state(ASSISTANT_STATE_THINKING);
            err = voice_client_listen(true, utterance, samples * sizeof(int16_t), &result);
        }

        if (err != ESP_OK) {
            set_state(ASSISTANT_STATE_NO_SERVER);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        if (result.action == VOICE_ACTION_REPLY) {
            speak(&result);
        }
        voice_result_free(&result);
    }
}

void assistant_start(void) {
    xTaskCreatePinnedToCore(assistant_task, "assistant", 8192, NULL, 5, NULL, 1);
}

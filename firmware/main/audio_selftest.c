#include "audio_selftest.h"
#include "audio_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "AUDIO_TEST";

#define SAMPLE_RATE 16000
#define RECORD_SECONDS 4
#define RECORD_SAMPLES (SAMPLE_RATE * RECORD_SECONDS)
#define TONE_CHUNK 256

static const char *volatile pending_status = NULL;

static void set_status(const char *text) {
    pending_status = text;
}

const char *audio_selftest_take_status(void) {
    const char *text = pending_status;
    pending_status = NULL;
    return text;
}

static void play_tone(float frequency, int milliseconds, float amplitude) {
    int16_t chunk[TONE_CHUNK];
    int total = SAMPLE_RATE * milliseconds / 1000;
    int done = 0;

    while (done < total) {
        int count = total - done;
        if (count > TONE_CHUNK) count = TONE_CHUNK;
        for (int i = 0; i < count; i++) {
            chunk[i] = (int16_t)(amplitude * sinf(2.0f * (float)M_PI * frequency * (done + i) / SAMPLE_RATE));
        }
        audio_write_samples(chunk, count * sizeof(int16_t));
        done += count;
    }
}

static void log_levels(const int16_t *samples, size_t count) {
    int64_t energy = 0;
    int peak = 0;
    for (size_t i = 0; i < count; i++) {
        int value = abs((int)samples[i]);
        if (value > peak) peak = value;
        energy += (int64_t)samples[i] * samples[i];
    }
    ESP_LOGI(TAG, "Mic levels: rms=%d peak=%d (of 32767)", (int)sqrt((double)energy / count), peak);
}

static void selftest_task(void *arg) {
    int16_t *recording = heap_caps_malloc(RECORD_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!recording) {
        ESP_LOGE(TAG, "No memory for recording");
        set_status("Audio test failed");
        vTaskDelete(NULL);
        return;
    }

    for (int round = 1;; round++) {
        ESP_LOGI(TAG, "Round %d: speaker tone", round);
        set_status("Speaker test");
        audio_set_speaker(true);
        vTaskDelay(pdMS_TO_TICKS(20));
        play_tone(440.0f, 400, 9000.0f);
        play_tone(660.0f, 400, 9000.0f);
        audio_set_speaker(false);

        ESP_LOGI(TAG, "Round %d: recording", round);
        set_status("Speak now...");
        vTaskDelay(pdMS_TO_TICKS(200));
        size_t got = 0;
        esp_err_t ret = audio_read_samples(recording, RECORD_SAMPLES * sizeof(int16_t), &got);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Recording failed: %s", esp_err_to_name(ret));
            set_status("Mic read failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        log_levels(recording, got / sizeof(int16_t));

        ESP_LOGI(TAG, "Round %d: playback", round);
        set_status("Playing back");
        audio_set_speaker(true);
        vTaskDelay(pdMS_TO_TICKS(20));
        audio_write_samples(recording, got);
        vTaskDelay(pdMS_TO_TICKS(100));
        audio_set_speaker(false);

        set_status("Waiting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void audio_selftest_start(void) {
    xTaskCreatePinnedToCore(selftest_task, "audio_test", 6144, NULL, 4, NULL, 1);
}

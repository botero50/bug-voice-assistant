#include "audio_circular_buffer.h"
#include <string.h>
#include <esp_attr.h>
#include <esp_log.h>

static const char *TAG = "AUDIO_BUFFER";

typedef struct {
    int16_t buffer[CIRCULAR_BUFFER_SIZE];
    volatile int write_pos;
    volatile int read_pos;
    volatile int available_samples;
} CircularAudioBuffer;

// 160 KB: kept in PSRAM so internal RAM stays free for the display and Wi-Fi.
EXT_RAM_BSS_ATTR static CircularAudioBuffer audio_buf;

void audio_circular_buffer_init(void) {
    memset(&audio_buf, 0, sizeof(audio_buf));
    ESP_LOGI(TAG, "Circular audio buffer initialized (capacity: %d samples, %.2f sec @ 16kHz)",
             CIRCULAR_BUFFER_SIZE, (float)CIRCULAR_BUFFER_SIZE / 16000.0);
}

void audio_circular_buffer_write(const int16_t *samples, int count) {
    if (!samples || count <= 0) return;

    for (int i = 0; i < count; i++) {
        audio_buf.buffer[audio_buf.write_pos] = samples[i];
        audio_buf.write_pos = (audio_buf.write_pos + 1) % CIRCULAR_BUFFER_SIZE;

        // Prevent buffer overflow - if we've wrapped, advance read pointer
        if (audio_buf.available_samples < CIRCULAR_BUFFER_SIZE) {
            audio_buf.available_samples++;
        } else {
            audio_buf.read_pos = (audio_buf.read_pos + 1) % CIRCULAR_BUFFER_SIZE;
        }
    }
}

int audio_circular_buffer_read(int16_t *out, int count) {
    if (!out || count <= 0) return 0;

    int read_count = 0;
    while (read_count < count && audio_buf.available_samples > 0) {
        out[read_count++] = audio_buf.buffer[audio_buf.read_pos];
        audio_buf.read_pos = (audio_buf.read_pos + 1) % CIRCULAR_BUFFER_SIZE;
        audio_buf.available_samples--;
    }

    return read_count;
}

int audio_circular_buffer_available(void) {
    return audio_buf.available_samples;
}

void audio_circular_buffer_flush(void) {
    audio_buf.write_pos = 0;
    audio_buf.read_pos = 0;
    audio_buf.available_samples = 0;
    ESP_LOGI(TAG, "Circular buffer flushed");
}

float audio_circular_buffer_fill_percent(void) {
    return (100.0f * audio_buf.available_samples) / CIRCULAR_BUFFER_SIZE;
}

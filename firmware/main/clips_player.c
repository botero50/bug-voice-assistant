#include "clips_player.h"
#include "audio_driver.h"
#include "voice_clips.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "CLIPS";

#define GAP_SAMPLES 700  // a short pause between words (16 kHz)

static const esp_partition_t *partition = NULL;
static uint32_t offsets[CLIP_COUNT];
static uint32_t lengths[CLIP_COUNT];
static bool ready = false;

bool clips_init(void) {
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!partition) {
        ESP_LOGW(TAG, "No storage partition");
        return false;
    }
    uint8_t header[8];
    if (esp_partition_read(partition, 0, header, sizeof(header)) != ESP_OK || memcmp(header, "BUGV", 4) != 0) {
        ESP_LOGW(TAG, "The storage partition holds no voice clips");
        return false;
    }
    uint32_t count;
    memcpy(&count, header + 4, 4);
    if (count != CLIP_COUNT) {
        ESP_LOGW(TAG, "The clips (%u) do not match this firmware (%u)", (unsigned)count, (unsigned)CLIP_COUNT);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry[2];
        if (esp_partition_read(partition, 8 + i * 8, entry, sizeof(entry)) != ESP_OK) {
            return false;
        }
        offsets[i] = entry[0];
        lengths[i] = entry[1];
    }
    ready = true;
    ESP_LOGI(TAG, "%u voice clips ready", (unsigned)count);
    return true;
}

void clips_play(const int *ids, int count) {
    if (!ready) {
        return;
    }
    static int16_t silence[GAP_SAMPLES];
    int16_t chunk[1024];
    for (int n = 0; n < count; n++) {
        int id = ids[n];
        if (id < 0 || id >= CLIP_COUNT) {
            continue;
        }
        uint32_t done = 0;
        while (done < lengths[id]) {
            uint32_t want = lengths[id] - done;
            if (want > sizeof(chunk)) want = sizeof(chunk);
            if (esp_partition_read(partition, offsets[id] + done, chunk, want) != ESP_OK) {
                return;
            }
            audio_write_samples(chunk, want);
            done += want;
        }
        if (n + 1 < count) {
            audio_write_samples(silence, sizeof(silence));
        }
    }
}

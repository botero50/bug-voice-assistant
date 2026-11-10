#include "mem_report.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MEM";

#define NOTED_MAX 12
static struct {
    const char *name;
    unsigned stack_bytes;
    TaskHandle_t handle;
} noted[NOTED_MAX];

void mem_task_note(const char *name, unsigned stack_bytes) {
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < NOTED_MAX; i++) {  // the same name again (a task that comes and goes) takes its old place
        if (noted[i].name && strcmp(noted[i].name, name) == 0) {
            noted[i].handle = me;
            return;
        }
    }
    for (int i = 0; i < NOTED_MAX; i++) {
        if (!noted[i].name) {
            noted[i].name = name;
            noted[i].stack_bytes = stack_bytes;
            noted[i].handle = me;
            return;
        }
    }
}

void mem_report(const char *why) {
    multi_heap_info_t internal, external, dma;
    heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&external, MALLOC_CAP_SPIRAM);
    heap_caps_get_info(&dma, MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "[%s] internal: %u KB free of %u KB, largest block %u B, lowest ever %u KB | external: %u KB free of %u KB | DMA-capable free %u KB",
             why, (unsigned)(internal.total_free_bytes / 1024), (unsigned)((internal.total_free_bytes + internal.total_allocated_bytes) / 1024),
             (unsigned)internal.largest_free_block, (unsigned)(internal.minimum_free_bytes / 1024), (unsigned)(external.total_free_bytes / 1024),
             (unsigned)((external.total_free_bytes + external.total_allocated_bytes) / 1024), (unsigned)(dma.total_free_bytes / 1024));
    for (int i = 0; i < NOTED_MAX; i++) {
        if (!noted[i].name || !noted[i].handle || eTaskGetState(noted[i].handle) == eDeleted) continue;
        unsigned never_used = (unsigned)uxTaskGetStackHighWaterMark(noted[i].handle);  // bytes in ESP-IDF
        ESP_LOGI(TAG, "[%s] task %-10s stack %5u B, never used %5u B (used at most %3u%%)", why, noted[i].name, noted[i].stack_bytes, never_used,
                 100 - 100 * never_used / noted[i].stack_bytes);
    }
}

#include "alarms.h"
#include "audio_driver.h"
#include "music_player.h"
#include "settings_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ALARMS";

#define MAX_ALARMS 8
#define CLOCK_VALID_AFTER 1700000000  // before Nov 2023 means the clock hasn't been set yet
#define MISSED_AFTER_SECONDS 300
#define RING_SECONDS 45
#define RING_MIN_VOLUME 85

static alarm_entry_t entries[MAX_ALARMS];

static void save(void) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save the alarms");
        return;
    }
    nvs_set_blob(handle, "alarms", entries, sizeof(entries));
    nvs_commit(handle);
    nvs_close(handle);
}

void alarms_init(void) {
    memset(entries, 0, sizeof(entries));
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(entries);
        if (nvs_get_blob(handle, "alarms", entries, &size) != ESP_OK || size != sizeof(entries)) {
            memset(entries, 0, sizeof(entries));
        }
        nvs_close(handle);
    }
    int count = 0;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at) count++;
    }
    ESP_LOGI(TAG, "%d alarms saved on the board", count);
}

uint32_t alarms_new_id(void) {
    uint32_t highest = 1000;  // the PC's ids are small numbers; these never collide with them
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at && entries[i].id > highest) highest = entries[i].id;
    }
    return highest + 1;
}

int alarms_cancel_kind(int kind) {
    int removed = 0;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at && (kind == 0 || entries[i].kind == kind)) {
            entries[i].at = 0;
            removed++;
        }
    }
    if (removed) save();
    return removed;
}

int alarms_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at) count++;
    }
    return count;
}

bool alarms_get_sorted(int index, alarm_entry_t *out) {
    bool used[MAX_ALARMS] = {false};
    for (int rank = 0; rank <= index; rank++) {
        int best = -1;
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (entries[i].at && !used[i] && (best < 0 || entries[i].at < entries[best].at)) best = i;
        }
        if (best < 0) return false;
        used[best] = true;
        if (rank == index) {
            *out = entries[best];
            return true;
        }
    }
    return false;
}

void alarms_add(uint32_t id, time_t at, const char *label, uint32_t repeat, uint8_t kind) {
    int slot = -1;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at && entries[i].id == id) {
            slot = i;  // the same id again replaces it
            break;
        }
        if (!entries[i].at && slot < 0) slot = i;
    }
    if (slot < 0) {
        slot = 0;  // full: the oldest slot is reused
        for (int i = 1; i < MAX_ALARMS; i++) {
            if (entries[i].at < entries[slot].at) slot = i;
        }
    }
    entries[slot].id = id;
    entries[slot].at = at;
    entries[slot].repeat = repeat;
    entries[slot].kind = kind;
    strlcpy(entries[slot].label, label, sizeof(entries[slot].label));
    save();
    ESP_LOGI(TAG, "Saved #%u \"%s\" for %lld (in %lld s)", (unsigned)id, label, (long long)at,
             (long long)(at - time(NULL)));
}

void alarms_cancel_ids(const char *ids) {
    char copy[96];
    strlcpy(copy, ids, sizeof(copy));
    char *save_ptr = NULL;
    for (char *token = strtok_r(copy, ",", &save_ptr); token; token = strtok_r(NULL, ",", &save_ptr)) {
        uint32_t id = (uint32_t)strtoul(token, NULL, 10);
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (entries[i].at && entries[i].id == id) {
                entries[i].at = 0;
                ESP_LOGI(TAG, "Cancelled #%u", (unsigned)id);
            }
        }
    }
    save();
}

bool alarms_next(alarm_entry_t *out) {
    time_t now = time(NULL);
    if (now < CLOCK_VALID_AFTER) {
        return false;
    }
    int best = -1;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (entries[i].at && entries[i].at > now && (best < 0 || entries[i].at < entries[best].at)) {
            best = i;
        }
    }
    if (best < 0) {
        return false;
    }
    *out = entries[best];
    return true;
}

bool alarms_pop_due(alarm_entry_t *out) {
    time_t now = time(NULL);
    if (now < CLOCK_VALID_AFTER) {
        return false;
    }
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!entries[i].at || entries[i].at > now) {
            continue;
        }
        bool missed = now - entries[i].at > MISSED_AFTER_SECONDS;
        alarm_entry_t due = entries[i];
        if (entries[i].repeat) {
            // Rings again at the next time; times missed while the board was off are skipped
            while (entries[i].at <= now) {
                entries[i].at += entries[i].repeat;
            }
        } else {
            entries[i].at = 0;
        }
        save();
        if (missed) {
            ESP_LOGW(TAG, "\"%s\" was missed while the board was off", due.label);
            continue;
        }
        *out = due;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- ringing
static volatile bool ringing = false;
static volatile bool ring_stop = false;
static char ring_label[48] = "";
static SemaphoreHandle_t ring_signal = NULL;

// One task is created at startup, while memory is plentiful, and sleeps until something rings.
// (Creating it at the moment of ringing can fail on a busy board: screen up, no sound, stuck.)
static void ring_task(void *arg) {
    for (;;) {
        xSemaphoreTake(ring_signal, portMAX_DELAY);

        bug_settings_t settings;
        settings_copy(&settings);
        int loud = settings.volume > RING_MIN_VOLUME ? settings.volume : RING_MIN_VOLUME;
        ESP_LOGI(TAG, "Ringing \"%s\" at volume %d", ring_label, loud);

        music_pause_for_voice();
        TickType_t started = xTaskGetTickCount();
        int bursts = 0;
        int write_errors = 0;
        while (!ring_stop && pdTICKS_TO_MS(xTaskGetTickCount() - started) < RING_SECONDS * 1000) {
            // Set again before every burst, so nothing else switching the speaker off can silence it
            audio_set_speaker(true);
            audio_set_volume(loud);
            vTaskDelay(pdMS_TO_TICKS(20));
            for (int i = 0; i < 3 && !ring_stop; i++) {
                if (audio_beep(1320.0f, 110) != ESP_OK) {
                    write_errors++;
                }
                vTaskDelay(pdMS_TO_TICKS(90));
            }
            bursts++;
            vTaskDelay(pdMS_TO_TICKS(600));
        }

        audio_set_speaker(false);
        audio_set_volume(settings.volume);
        ESP_LOGI(TAG, "Ringing ended after %d bursts (%d audio errors), %s", bursts, write_errors,
                 ring_stop ? "stopped by the user" : "timed out");
        ringing = false;
        music_resume_after_voice();
    }
}

void alarms_start_ringer(void) {
    ring_signal = xSemaphoreCreateBinary();
    if (!ring_signal || xTaskCreatePinnedToCore(ring_task, "alarm_ring", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Could not create the ringing task: alarms will show but stay silent");
        ring_signal = NULL;
    }
}

void alarm_ring_start(const char *label) {
    if (ringing) {
        ESP_LOGW(TAG, "Already ringing, ignoring \"%s\"", label);
        return;
    }
    strlcpy(ring_label, label, sizeof(ring_label));
    if (!ring_signal) {
        ESP_LOGE(TAG, "No ringing task, so \"%s\" cannot sound", label);
        return;
    }
    ring_stop = false;
    ringing = true;
    xSemaphoreGive(ring_signal);
}

void alarm_ring_stop(void) {
    ring_stop = true;
}

bool alarm_ringing(void) {
    return ringing;
}

const char *alarm_ring_label(void) {
    return ring_label;
}

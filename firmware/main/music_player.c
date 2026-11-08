#include "music_player.h"
#include "audio_driver.h"
#include "settings_store.h"
#include "voice_client.h"
#include <strings.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "MUSIC";

#define READ_BYTES 4096
#define GIVE_UP_AFTER_FAILURES 4

static volatile bool running = false;       // the task exists
static volatile bool stop_request = false;
static volatile bool wanted = false;        // the user wants music, even if it is paused right now
static volatile float level = 0;
static char stream_path[160];
static char label[80] = "Radio";
static char track_title[80] = "";
static char track_artist[60] = "";
static volatile int track_serial = 0;       // goes up each time a new song's name arrives

// Songs from the PC's library send their name in the response headers; radio sends none.
static esp_err_t header_cb(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }
    if (strcasecmp(event->header_key, "X-Title") == 0) {
        voice_client_percent_decode(track_title, sizeof(track_title), event->header_value);
        track_serial++;
    } else if (strcasecmp(event->header_key, "X-Artist") == 0) {
        voice_client_percent_decode(track_artist, sizeof(track_artist), event->header_value);
    }
    return ESP_OK;
}

const char *music_track_title(void) {
    return track_title;
}

const char *music_track_artist(void) {
    return track_artist;
}

int music_track_serial(void) {
    return track_serial;
}

void music_set_label(const char *station_name) {
    strlcpy(label, station_name, sizeof(label));
}

const char *music_label(void) {
    return label;
}

bool music_is_playing(void) {
    return running && !stop_request;
}

bool music_wanted(void) {
    return wanted;
}

float music_level(void) {
    return level;
}

static void publish_level(const int16_t *samples, size_t count) {
    double sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (double)samples[i] * samples[i];
    }
    float rms = count ? sqrtf((float)(sum / count)) : 0;
    // 0 at a quiet 300, 1 about 25 dB above that
    float value = 20.0f * log10f(fmaxf(rms, 1.0f) / 300.0f) / 25.0f;
    level = value < 0 ? 0 : (value > 1 ? 1 : value);
}

// Plays until the stream ends, drops, or a stop is requested.
static void run_stream(void) {
    bug_settings_t settings;
    settings_copy(&settings);
    char url[260];
    snprintf(url, sizeof(url), "%s/%s", settings.server_url, stream_path);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .disable_auto_redirect = true,
        .event_handler = header_cb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return;
    }
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGW(TAG, "Could not reach the server");
        esp_http_client_cleanup(client);
        return;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "Server answered HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    ESP_LOGI(TAG, "Streaming %s", stream_path);
    audio_set_speaker(true);

    // The stream is chunked, so reads arrive in any size; keep an odd leftover byte for the next round.
    int16_t samples_buffer[READ_BYTES / 2];
    uint8_t *bytes = (uint8_t *)samples_buffer;
    int carried = 0;
    int empty_reads = 0;
    while (!stop_request) {
        int got = esp_http_client_read(client, (char *)bytes + carried, READ_BYTES - carried);
        if (got < 0) {
            break;
        }
        if (got == 0) {
            if (++empty_reads > 5) break;
            continue;
        }
        empty_reads = 0;

        int total = carried + got;
        int sample_count = total / 2;
        if (sample_count > 0) {
            publish_level(samples_buffer, sample_count);
            audio_write_samples(samples_buffer, sample_count * 2);
        }
        carried = total & 1;
        if (carried) {
            bytes[0] = bytes[total - 1];
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void music_task(void *arg) {
    int quick_failures = 0;
    while (!stop_request) {
        TickType_t started = xTaskGetTickCount();
        run_stream();
        if (stop_request) {
            break;
        }
        // The stream dropped. Try again, but give up if it keeps failing right away.
        quick_failures = pdTICKS_TO_MS(xTaskGetTickCount() - started) < 5000 ? quick_failures + 1 : 0;
        if (quick_failures >= GIVE_UP_AFTER_FAILURES) {
            ESP_LOGW(TAG, "The stream keeps failing, giving up");
            wanted = false;
            break;
        }
        // A song that played for a while just ended: the next one follows almost at once
        bool played_long = pdTICKS_TO_MS(xTaskGetTickCount() - started) >= 5000;
        vTaskDelay(pdMS_TO_TICKS(played_long ? 150 : 1500));
    }
    audio_set_speaker(false);
    level = 0;
    running = false;
    vTaskDelete(NULL);
}

static void stop_task(void) {
    if (!running) {
        return;
    }
    stop_request = true;
    for (int i = 0; i < 100 && running; i++) {  // up to 2 s
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void start_task(void) {
    if (running) {
        return;
    }
    stop_request = false;
    running = true;
    xTaskCreatePinnedToCore(music_task, "music", 10240, NULL, 4, NULL, 1);
}

void music_start(const char *path) {
    stop_task();
    strlcpy(stream_path, path, sizeof(stream_path));
    wanted = true;
    start_task();
}

static volatile uint32_t sleep_at_ms = 0;  // 0 means no sleep timer

static uint32_t uptime_ms(void) {
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

void music_sleep_in(int seconds) {
    sleep_at_ms = seconds > 0 ? uptime_ms() + (uint32_t)seconds * 1000 : 0;
    if (sleep_at_ms == 0 && seconds > 0) sleep_at_ms = 1;
    ESP_LOGI(TAG, "Music sleep timer: %d s", seconds);
}

bool music_sleep_expired(void) {
    if (!sleep_at_ms || (int32_t)(uptime_ms() - sleep_at_ms) < 0) {
        return false;
    }
    sleep_at_ms = 0;
    return true;
}

void music_stop(void) {
    wanted = false;
    sleep_at_ms = 0;
    stop_task();
}

void music_pause_for_voice(void) {
    stop_task();
}

void music_resume_after_voice(void) {
    if (wanted && !running) {
        start_task();
    }
}

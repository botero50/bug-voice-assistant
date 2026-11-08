#include "music_player.h"
#include "local_skills.h"
#include "audio_driver.h"
#include "settings_store.h"
#include "voice_client.h"
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mp3dec.h"
#include <dirent.h>
#include <stdlib.h>
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
static char stream_path[260];
static char label[80] = "Radio";
static char track_title[80] = "";
static char track_artist[60] = "";
static volatile int skip_request = 0;       // +1 next song, -1 previous (music from the card)
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

// ---------------------------------------------------------------------------------- MP3 on the board
// The board decodes MP3 itself (Helix), from a radio stream it opens or from a file on the microSD card. The bus runs
// at 32 kHz mono, so other rates are converted with a straight line between samples.
#define MP3_IN_BYTES 16384
#define BUS_RATE 32000

typedef int (*read_fn)(void *context, uint8_t *buffer, int size);

static int http_read(void *context, uint8_t *buffer, int size) {
    int got = esp_http_client_read((esp_http_client_handle_t)context, (char *)buffer, size);
    return got < 0 ? -1 : got;
}

static int file_read(void *context, uint8_t *buffer, int size) {
    return (int)fread(buffer, 1, (size_t)size, (FILE *)context);
}

// Decodes until the source ends, a stop is requested or (for files) a skip is asked. Returns the frames played.
static int play_mp3(read_fn read, void *context, bool is_file, bool honor_stop) {
    HMP3Decoder decoder = MP3InitDecoder();
    uint8_t *input = heap_caps_malloc(MP3_IN_BYTES, MALLOC_CAP_SPIRAM);
    int16_t *pcm = heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *mono = heap_caps_malloc(1152 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *out = heap_caps_malloc(1152 * 2 * sizeof(int16_t) + 64, MALLOC_CAP_SPIRAM);  // up to 2x for 16 kHz sources
    int frames = 0;
    if (!decoder || !input || !pcm || !mono || !out) {
        ESP_LOGW(TAG, "Not enough memory for the MP3 decoder");
        goto done;
    }

    int filled = 0, empty_reads = 0;
    bool source_ended = false;
    double pos = -1.0;
    int16_t last = 0;
    while ((!honor_stop || !stop_request) && !(is_file && skip_request)) {
        if (filled < MP3_IN_BYTES / 2 && !source_ended) {
            int got = read(context, input + filled, MP3_IN_BYTES - filled);
            if (got < 0) {
                source_ended = true;
            } else if (got == 0) {
                if (is_file || ++empty_reads > 5) source_ended = true;
            } else {
                empty_reads = 0;
                filled += got;
            }
        }
        if (filled < 4) {
            if (source_ended) break;
            continue;
        }
        int sync = MP3FindSyncWord(input, filled);
        if (sync < 0) {
            filled = 0;  // nothing that looks like audio: take more
            continue;
        }
        if (sync > 0) {
            memmove(input, input + sync, filled - sync);
            filled -= sync;
        }
        unsigned char *cursor = input;
        int left = filled;
        int error = MP3Decode(decoder, &cursor, &left, pcm, 0);
        int used = filled - left;
        if (error == ERR_MP3_INDATA_UNDERFLOW || error == ERR_MP3_MAINDATA_UNDERFLOW) {
            if (source_ended) break;
            if (used > 0) {
                memmove(input, input + used, left);
                filled = left;
            }
            continue;
        }
        if (used <= 0) used = 1;
        memmove(input, input + used, filled - used);
        filled -= used;
        if (error != ERR_MP3_NONE) {
            continue;  // a damaged frame: skip it
        }
        MP3FrameInfo info;
        MP3GetLastFrameInfo(decoder, &info);
        if (info.samprate < 8000 || info.outputSamps <= 0) continue;
        int count = info.outputSamps / (info.nChans > 0 ? info.nChans : 1);
        for (int i = 0; i < count; i++) {
            mono[i] = info.nChans == 2 ? (int16_t)(((int)pcm[2 * i] + pcm[2 * i + 1]) / 2) : pcm[i];
        }
        int produced = 0;
        if (info.samprate == BUS_RATE) {
            memcpy(out, mono, count * sizeof(int16_t));
            produced = count;
        } else {
            double step = (double)info.samprate / BUS_RATE;
            while ((int)floor(pos) + 1 < count && produced < 1152 * 2) {
                int i = (int)floor(pos);
                double f = pos - i;
                int a = i < 0 ? last : mono[i], b = mono[i + 1];
                out[produced++] = (int16_t)(a + (b - a) * f);
                pos += step;
            }
            pos -= count;
            last = mono[count - 1];
        }
        if (produced > 0) {
            publish_level(out, produced);
            audio_write_music(out, produced * 2);
        }
        frames++;
    }
done:
    if (decoder) MP3FreeDecoder(decoder);
    heap_caps_free(input);
    heap_caps_free(pcm);
    heap_caps_free(mono);
    heap_caps_free(out);
    return frames;
}

typedef struct {
    const uint8_t *data;
    size_t size, pos;
} memory_source_t;

static int memory_read(void *context, uint8_t *buffer, int size) {
    memory_source_t *source = (memory_source_t *)context;
    size_t left = source->size - source->pos;
    int n = (int)(left < (size_t)size ? left : (size_t)size);
    memcpy(buffer, source->data + source->pos, (size_t)n);
    source->pos += (size_t)n;
    return n;
}

// Plays an MP3 that is already in memory (the online voice). It ignores the music's stop request.
bool music_play_mp3_memory(const uint8_t *data, size_t size) {
    memory_source_t source = {data, size, 0};
    audio_set_speaker(true);
    int frames = play_mp3(memory_read, &source, true, false);
    return frames > 0;
}

// A radio station: the stream's address is the path itself
static char redirect_to[300];
static char content_type[64];

// esp_http_client_get_header() reads the request's headers, so the answer's headers come through this
static esp_err_t station_header_cb(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(event->header_key, "Location") == 0) strlcpy(redirect_to, event->header_value, sizeof(redirect_to));
        else if (strcasecmp(event->header_key, "Content-Type") == 0) strlcpy(content_type, event->header_value, sizeof(content_type));
    }
    return ESP_OK;
}

static void run_direct(void) {
    esp_http_client_config_t config = {
        .url = stream_path,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .user_agent = "BugVoiceAssistant/0.7",
        .event_handler = station_header_cb,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    int status = 0;
    for (int hop = 0; hop < 4; hop++) {  // open() does not follow redirects: do it here
        redirect_to[0] = '\0';
        content_type[0] = '\0';
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGW(TAG, "Could not reach the station");
            esp_http_client_cleanup(client);
            return;
        }
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) break;
        if (!redirect_to[0]) break;
        char next[300];
        strlcpy(next, redirect_to, sizeof(next));
        ESP_LOGI(TAG, "Redirected to %s", next);
        esp_http_client_close(client);
        esp_http_client_set_url(client, next);
    }
    const char *type = content_type[0] ? content_type : NULL;
    if (status != 200 || (type && (strcasestr(type, "aac") || strcasestr(type, "ogg") || strcasestr(type, "mp4")))) {
        ESP_LOGW(TAG, "The station cannot be played here (HTTP %d, %s)", status, type ? type : "?");
    } else {
        ESP_LOGI(TAG, "Playing the station on the board");
        audio_set_speaker(true);
        play_mp3(http_read, client, false, true);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

// Music from the microSD card: every .mp3 in /sd/music, in name order, then again
#define MAX_SONGS 200
static char *songs[MAX_SONGS];
static int song_count = 0, song_index = 0;

static int compare_names(const void *a, const void *b) {
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static void scan_songs(void) {
    for (int i = 0; i < song_count; i++) free(songs[i]);
    song_count = 0;
    DIR *dir = opendir("/sd/music");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && song_count < MAX_SONGS) {
        size_t n = strlen(entry->d_name);
        if (n > 4 && strcasecmp(entry->d_name + n - 4, ".mp3") == 0) songs[song_count++] = strdup(entry->d_name);
    }
    closedir(dir);
    qsort(songs, song_count, sizeof(songs[0]), compare_names);
}

static void run_card(void) {
    if (song_count == 0) scan_songs();
    if (song_count == 0) {
        ESP_LOGW(TAG, "No MP3 files in /sd/music");
        return;
    }
    audio_set_speaker(true);
    while (!stop_request) {
        char path[300];
        snprintf(path, sizeof(path), "/sd/music/%s", songs[song_index]);
        strlcpy(track_title, songs[song_index], sizeof(track_title));
        char *dot = strrchr(track_title, '.');
        if (dot) *dot = '\0';
        strlcpy(track_artist, "Tarjeta SD", sizeof(track_artist));
        track_serial++;
        FILE *file = fopen(path, "rb");
        int step = 1;
        if (file) {
            unsigned char head[10];
            if (fread(head, 1, 10, file) == 10 && memcmp(head, "ID3", 3) == 0) {
                long size = ((head[6] & 0x7F) << 21) | ((head[7] & 0x7F) << 14) | ((head[8] & 0x7F) << 7) | (head[9] & 0x7F);
                fseek(file, 10 + size, SEEK_SET);
            } else {
                fseek(file, 0, SEEK_SET);
            }
            skip_request = 0;
            play_mp3(file_read, file, true, true);
            fclose(file);
            if (skip_request) step = skip_request;
        }
        skip_request = 0;
        song_index = ((song_index + step) % song_count + song_count) % song_count;
    }
}

void music_set_now_playing(const char *name, const char *detail) {
    strlcpy(track_title, name, sizeof(track_title));
    strlcpy(track_artist, detail, sizeof(track_artist));
    track_serial++;
}

bool music_card_playing(void) {
    return wanted && strncmp(stream_path, "sd:", 3) == 0;
}

void music_next_song(int direction) {
    skip_request = direction < 0 ? -1 : 1;
}

bool music_card_has_songs(void) {
    scan_songs();
    return song_count > 0;
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
            audio_write_music(samples_buffer, sample_count * 2);
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
        if (strncmp(stream_path, "http", 4) == 0) run_direct();
        else if (strncmp(stream_path, "sd:", 3) == 0) run_card();
        else run_stream();
        if (stop_request) {
            break;
        }
        // A radio station that did not play: try the next one from the search, at once
        if (strncmp(stream_path, "http", 4) == 0 && pdTICKS_TO_MS(xTaskGetTickCount() - started) < 8000) {
            const char *next = local_radio_failover();
            if (next) {
                strlcpy(stream_path, next, sizeof(stream_path));
                quick_failures = 0;
                continue;
            }
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
    ESP_LOGI(TAG, "Starting the music task (free internal RAM %u, largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (xTaskCreatePinnedToCore(music_task, "music", 10240, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Not enough memory to start the music task");
        running = false;
        wanted = false;
    }
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

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "local_skills.h"
#include "local_skills_internal.h"
#include "music_player.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A free online voice for text the board has no recording of (news, Wikipedia, jokes). The board asks Google Translate's
// text-to-speech service for an MP3 of each piece of the text (no key needed), decodes the MP3 itself and plays it.
// It needs internet; without it the text is only shown. The service takes short pieces, so the text is cut at the
// ends of sentences.
//
// Two things keep the silence between sentences short: all the pieces of one answer travel over ONE open secure connection (each new one costs
// about 1.4 s of handshake on the board), and while a piece is playing a small task already fetches the next one (see docs/IMPROVEMENT_PLAN.md).

static const char *TAG = "TTS";

#define PIECE_BYTES 160
#define PREFETCH_STACK 6144   // a reconnection (a full TLS handshake) can happen inside this task
#define PREFETCH_WAIT_MS 15000

static void url_encode(const char *in, size_t length, char *out, size_t size) {
    size_t o = 0;
    for (size_t i = 0; i < length && o + 4 < size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else o += (size_t)snprintf(out + o, size - o, "%%%02X", c);
    }
    out[o] = '\0';
}

// Where to cut the text: the end of the last sentence (or comma, or word) that fits
static size_t cut_point(const char *text) {
    size_t length = strlen(text);
    if (length <= PIECE_BYTES) return length;
    size_t best_sentence = 0, best_comma = 0, best_space = 0;
    for (size_t i = 0; i < PIECE_BYTES; i++) {
        char c = text[i];
        if ((c == '.' || c == '!' || c == '?') && (text[i + 1] == ' ' || text[i + 1] == '\0')) best_sentence = i + 1;
        else if ((c == ',' || c == ';' || c == ':') && text[i + 1] == ' ') best_comma = i + 1;
        else if (c == ' ') best_space = i;
    }
    size_t cut = best_sentence > 60 ? best_sentence : best_comma > 60 ? best_comma : best_space > 20 ? best_space : PIECE_BYTES;
    while (cut > 0 && cut < length && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;  // not in the middle of a letter
    return cut;
}

typedef struct {
    uint8_t *data;
    size_t size, capacity;
} sink_t;

static sink_t *volatile s_target = NULL;  // the client's user_data points here: which sink receives the data now (NULL: nothing, the answer is dropped)

// Where the HTTP client writes the MP3 it receives: the piece being fetched now (the one playing is in the other sink)
static esp_err_t on_data(esp_http_client_event_t *event) {
    sink_t *sink = event->user_data ? *(sink_t *volatile *)event->user_data : NULL;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0 && sink) {
        if (sink->size + (size_t)event->data_len > sink->capacity) {
            size_t bigger = sink->capacity ? sink->capacity * 2 : 16384;
            while (bigger < sink->size + (size_t)event->data_len) bigger *= 2;
            uint8_t *moved = heap_caps_realloc(sink->data, bigger, MALLOC_CAP_SPIRAM);
            if (!moved) return ESP_FAIL;
            sink->data = moved;
            sink->capacity = bigger;
        }
        memcpy(sink->data + sink->size, event->data, (size_t)event->data_len);
        sink->size += (size_t)event->data_len;
    }
    return ESP_OK;
}

// Gets one piece (retrying once on a fresh connection if the open one had gone stale). The MP3 is in sink->data when it returns true.
static bool fetch_piece(esp_http_client_handle_t client, sink_t *sink, const char *url) {
    for (int attempt = 0; attempt < 2; attempt++) {
        sink->size = 0;
        esp_http_client_set_url(client, url);
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK && esp_http_client_get_status_code(client) == 200 && sink->size >= 200) return true;
        esp_http_client_close(client);  // the next attempt makes a new connection
    }
    return false;
}

typedef struct {
    esp_http_client_handle_t client;
    sink_t *volatile *target;  // the client's user_data: which sink receives the data
    sink_t *sink;
    char url[760];
    SemaphoreHandle_t done;
    bool ok;
    int64_t took_ms;
} job_t;

static void prefetch_task(void *arg) {
    job_t *job = arg;
    int64_t started = esp_timer_get_time();
    *job->target = job->sink;
    job->ok = fetch_piece(job->client, job->sink, job->url);
    job->took_ms = (esp_timer_get_time() - started) / 1000;
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);
}

// ---- A connection to the voice service opened ahead of time. The first word of an online answer waited for a secure handshake (about 1.4 s)
// that can happen while the text is still being fetched: a skill calls tts_prewarm() as soon as it knows it will speak.
typedef enum { WARM_IDLE, WARM_OPENING, WARM_READY } warm_state_t;
static volatile warm_state_t warm_state = WARM_IDLE;
static esp_http_client_handle_t warm_client = NULL;
static SemaphoreHandle_t warm_lock = NULL;      // guards the state changes
static SemaphoreHandle_t warm_opened = NULL;    // given when the opening task ends
static esp_timer_handle_t warm_timer = NULL;    // closes a warm connection nobody used
#define WARM_KEEP_US (25 * 1000000LL)

static esp_http_client_config_t voice_config(void) {
    esp_http_client_config_t config = {.url = "https://translate.google.com/", .timeout_ms = 8000, .buffer_size = 1024, .event_handler = on_data,
                                       .user_data = &s_target, .crt_bundle_attach = esp_crt_bundle_attach, .user_agent = "Mozilla/5.0 (BugVoiceAssistant)"};
    return config;
}

static void warm_expired(void *arg) {
    xSemaphoreTake(warm_lock, portMAX_DELAY);
    if (warm_state == WARM_READY && warm_client) {
        esp_http_client_cleanup(warm_client);
        warm_client = NULL;
        warm_state = WARM_IDLE;
        ESP_LOGI(TAG, "The warm voice connection was not used and was closed");
    }
    xSemaphoreGive(warm_lock);
}

static void warm_task(void *arg) {
    esp_http_client_config_t config = voice_config();
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool ok = false;
    if (client) {
        int64_t started = esp_timer_get_time();
        s_target = NULL;  // the tiny answer is not needed, only the open connection
        esp_http_client_set_url(client, "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=es&q=.");
        ok = esp_http_client_perform(client) == ESP_OK;
        ESP_LOGI(TAG, "Voice connection %s in %lld ms", ok ? "warm" : "could not be opened", (long long)((esp_timer_get_time() - started) / 1000));
    }
    xSemaphoreTake(warm_lock, portMAX_DELAY);
    if (ok) {
        warm_client = client;
        warm_state = WARM_READY;
        esp_timer_stop(warm_timer);
        esp_timer_start_once(warm_timer, WARM_KEEP_US);
    } else {
        if (client) esp_http_client_cleanup(client);
        warm_state = WARM_IDLE;
    }
    xSemaphoreGive(warm_lock);
    xSemaphoreGive(warm_opened);
    vTaskDelete(NULL);
}

void tts_prewarm(void) {
    if (!warm_lock) {
        warm_lock = xSemaphoreCreateMutex();
        warm_opened = xSemaphoreCreateBinary();
        const esp_timer_create_args_t args = {.callback = warm_expired, .name = "tts_warm"};
        if (!warm_lock || !warm_opened || esp_timer_create(&args, &warm_timer) != ESP_OK) return;
    }
    xSemaphoreTake(warm_lock, portMAX_DELAY);
    bool start = warm_state == WARM_IDLE;
    if (start) warm_state = WARM_OPENING;
    xSemaphoreGive(warm_lock);
    if (!start) return;
    xSemaphoreTake(warm_opened, 0);  // forget an old signal
    if (xTaskCreate(warm_task, "tts_warm", PREFETCH_STACK, NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(warm_lock, portMAX_DELAY);
        warm_state = WARM_IDLE;
        xSemaphoreGive(warm_lock);
    }
}

// The warm connection if there is one (waiting a little if it is still opening), or NULL
static esp_http_client_handle_t take_warm(void) {
    if (!warm_lock) return NULL;
    if (warm_state == WARM_OPENING) xSemaphoreTake(warm_opened, pdMS_TO_TICKS(7000));
    esp_http_client_handle_t client = NULL;
    xSemaphoreTake(warm_lock, portMAX_DELAY);
    if (warm_state == WARM_READY && warm_client) {
        esp_timer_stop(warm_timer);
        client = warm_client;
        warm_client = NULL;
        warm_state = WARM_IDLE;
    }
    xSemaphoreGive(warm_lock);
    return client;
}

// The URL of the piece that starts at *p (and how many bytes it covers), or false when the text is over
static bool next_url(const char **p, const char *lang, char *url, size_t url_size, size_t *cut_out) {
    while (**p == ' ') (*p)++;
    if (!**p) return false;
    size_t cut = cut_point(*p);
    if (cut == 0) return false;
    char encoded[PIECE_BYTES * 3 + 8];
    url_encode(*p, cut, encoded, sizeof(encoded));
    snprintf(url, url_size, "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=%s&q=%s", lang && lang[0] ? lang : "es", encoded);
    *cut_out = cut;
    return true;
}

bool tts_say_lang(const char *text, const char *lang) {
    bool played = false;
    sink_t sinks[2] = {{0}, {0}};
    esp_http_client_handle_t client = take_warm();
    bool was_warm = client != NULL;
    if (!client) {
        esp_http_client_config_t config = voice_config();
        client = esp_http_client_init(&config);
    }
    job_t *job = heap_caps_calloc(1, sizeof(job_t), MALLOC_CAP_SPIRAM);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!client || !job || !done) {
        if (client) esp_http_client_cleanup(client);
        heap_caps_free(job);
        if (done) vSemaphoreDelete(done);
        return false;
    }
    job->client = client;
    job->target = &s_target;
    job->done = done;

    const char *p = text;
    size_t cut = 0;
    char url[760];
    int current = 0;
    if (next_url(&p, lang, url, sizeof(url), &cut)) {
        // The first piece is fetched here, the others by the prefetch task while the one before plays
        int64_t asked = esp_timer_get_time();
        s_target = &sinks[current];
        if (fetch_piece(client, &sinks[current], url)) {
            ESP_LOGI(TAG, "Online voice piece fetched in %lld ms%s", (long long)((esp_timer_get_time() - asked) / 1000), was_warm ? " (on a warm connection)" : "");
            p += cut;
            for (;;) {
                bool more = next_url(&p, lang, job->url, sizeof(job->url), &cut);
                bool prefetching = false;
                if (more) {
                    job->sink = &sinks[1 - current];
                    job->ok = false;
                    prefetching = xTaskCreate(prefetch_task, "tts_prefetch", PREFETCH_STACK, job, 3, NULL) == pdPASS;
                    if (!prefetching) ESP_LOGW(TAG, "The next piece could not be prefetched (not enough memory), it will be fetched after this one");
                }
                bool ok = music_play_mp3_memory(sinks[current].data, sinks[current].size);
                ESP_LOGI(TAG, "Online voice piece: %u bytes, %s", (unsigned)sinks[current].size, ok ? "played" : "could not be decoded");
                if (ok) played = true;
                if (!more) break;
                if (prefetching) {
                    int64_t waited = esp_timer_get_time();
                    bool arrived = xSemaphoreTake(done, pdMS_TO_TICKS(PREFETCH_WAIT_MS)) == pdTRUE && job->ok;
                    ESP_LOGI(TAG, "Next piece: fetched in %lld ms while playing, waited %lld ms after it", (long long)job->took_ms,
                             (long long)((esp_timer_get_time() - waited) / 1000));
                    if (!arrived) {
                        ESP_LOGW(TAG, "The online voice did not answer");
                        break;
                    }
                } else {  // no task: fetch it here, as before
                    s_target = &sinks[1 - current];
                    if (!fetch_piece(client, &sinks[1 - current], job->url)) {
                        ESP_LOGW(TAG, "The online voice did not answer");
                        break;
                    }
                }
                p += cut;
                current = 1 - current;
            }
        } else {
            ESP_LOGW(TAG, "The online voice did not answer");
        }
    }
    esp_http_client_cleanup(client);
    vSemaphoreDelete(done);
    heap_caps_free(job);
    heap_caps_free(sinks[0].data);
    heap_caps_free(sinks[1].data);
    return played;
}

bool tts_say(const char *text) {
    return tts_say_lang(text, "es");
}

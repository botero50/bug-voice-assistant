#include "assistant.h"
#include "alarms.h"
#include "clips_player.h"
#include "command_local.h"
#include "server_finder.h"
#include "voice_clips.h"
#include "local_skills.h"
#include "audio_driver.h"
#include "clock.h"
#include "music_player.h"
#include "network_manager.h"
#include "ota_update.h"
#include "settings_store.h"
#include "stopwatch.h"
#include "voice_client.h"
#include "wake_local.h"
#include "wake_verify.h"
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
#define END_SILENCE_MS 650                         // a pause this long ends the sentence
#define KEEP_SILENCE_MS 300
#define MIN_SPEECH_FRAMES 8                        // ignore clicks, coughs and knocks shorter than 160 ms
#define COMMAND_WAIT_MS 6000                       // after "Hola Bug", wait this long for the command
#define MIN_THRESHOLD 220.0f                       // never trigger below this level
#define NOISE_MULTIPLIER 3.0f                      // speech starts this far above the room noise
#define LOG_INTERVAL_MS 10000

static volatile assistant_state_t current_state = ASSISTANT_STATE_CONNECTING;
static volatile bool state_changed = true;
static volatile float audio_level = 0;
static volatile bool wifi_dirty = false;
static volatile bool listen_now_request = false;
static volatile bool muted = false;
static char test_words[200];
static volatile bool test_words_pending = false;  // set by assistant_hear()
static volatile bool announce_pending = false;  // a message from the development PC is waiting to be said
static volatile bool wake_listening = false;  // the on-board detector is fed with the microphone
static volatile bool local_wake = false;       // it heard "Hola Bug"
static bool skip_chime = false;
static uint32_t pc_down_until_ms = 0;  // after the PC fails, the board does not try it again for a minute
static volatile bool train_requested = false;  // the owner asked to record their voice
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
    if (state == current_state) {
        return;  // nothing changes, so the screen is not told and nothing flickers or wakes up
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

// For testing from the USB cable: the board acts as if it had just heard these words (BUGCFG {"hear":"..."}); the assistant task picks them up

void assistant_hear(const char *words) {
    if (test_words_pending || !words) return;
    strlcpy(test_words, words, sizeof(test_words));
    test_words_pending = true;
}

// Opens a screen by its path, as if the voice had asked (used by the installer page's test command)
void assistant_open_screen(const char *path) {
    publish_screen(path, NULL);
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
    if (wake_listening && !muted && !music_is_playing() && !alarm_ringing() && wake_local_feed(frame)) {
        local_wake = true;
    }
    return true;
}

// The PC did not answer. If the Wi-Fi itself dropped, wait for it to come back (the network
// manager keeps retrying); otherwise show that the server is missing.
static bool pc_available(void) {
    // Without Wi-Fi the PC cannot be reached at all, so do not even try (a try would hold the board up for seconds)
    return network_manager_is_connected() == ESP_OK && (int32_t)(pdTICKS_TO_MS(xTaskGetTickCount()) - pc_down_until_ms) >= 0;
}

static void report_unreachable(void) {
    // The PC (or the internet) did not answer. The board carries on by itself, so nothing is shown; it just does
    // not try the PC again for a minute.
    set_texts("", "");
    pc_down_until_ms = pdTICKS_TO_MS(xTaskGetTickCount()) + 60000;
    ESP_LOGW(TAG, "The PC did not answer");
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

        if (interruptible && (listen_now_request || local_wake || announce_pending || test_words_pending || muted != muted_at_start || music_is_playing() != music_at_start)) {
            return 0;
        }

        memcpy(preroll[head], frame, sizeof(frame));
        head = (head + 1) % PREROLL_FRAMES;
        if (filled < PREROLL_FRAMES) filled++;

        // While music plays the mic would hear the music itself, so only a tap on the ring starts a command
        if (rms > threshold && !muted && !music_is_playing() && !alarm_ringing()) {
            ESP_LOGI(TAG, "Speech started: level %.0f, trigger %.0f, room noise %.0f", rms, threshold, noise_floor);
            publish_level(rms);
            if (speech_state != current_state) {  // in standby nothing is shown until the wake word is confirmed
                set_state(speech_state);
            }
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
        if (interruptible && local_wake) {
            audio_level = 0;
            return 0;  // the detector heard "Hola Bug" while this sentence was still being recorded
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

static char announce_text[300];

// A message from the development PC (see dev_bridge.c): said as soon as the board is idle
void assistant_announce(const char *text) {
    strlcpy(announce_text, text, sizeof(announce_text));
    announce_pending = true;
}

static void chime(void);

static void say_announcement(void) {
    char text[300];
    strlcpy(text, announce_text, sizeof(text));
    announce_pending = false;
    if (muted) return;
    music_pause_for_voice();
    audio_set_speaker(true);
    vTaskDelay(pdMS_TO_TICKS(30));
    chime();
    tts_say(text);  // needs internet; without it the chime and the text on the screen are all there is
    vTaskDelay(pdMS_TO_TICKS(150));
    audio_set_speaker(false);
    audio_flush_input(300);
    music_resume_after_voice();
}

static void chime(void) {
    audio_set_speaker(true);
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_beep(880.0f, 60);
    audio_beep(1175.0f, 80);
    audio_set_speaker(false);
    audio_flush_input(150);  // short, so the first words of the command are not lost
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

// Speaks an answer: the PC's recording, or (for answers the board made itself) its own voice clips.
static char pending_voice_lang[8] = "";
static char pending_voice[420] = "";  // text for the online voice, said after the clips

static void speak_with(const voice_result_t *result, const int *clips, int clip_count) {
    set_texts(result->heard, result->reply);

    // Some answers carry an action instead of, or as well as, a screen
    int volume_delta = 0;
    int volume_set = -1;
    bool start_music = false;
    bool start_ota = false;
    char music_path[260] = "";
    if (result->screen[0]) {
        char *json = NULL;
        if (strncmp(result->screen, "volume?d=", 9) == 0) {
            volume_delta = atoi(result->screen + 9);
        } else if (strncmp(result->screen, "stopwatch?cmd=", 14) == 0) {
            stopwatch_command(result->screen + 14);
            publish_screen("stopwatch", NULL);
        } else if (strncmp(result->screen, "train?", 6) == 0) {
            train_requested = true;  // starts once this answer has been spoken
        } else if (strncmp(result->screen, "ota?", 4) == 0) {
            start_ota = true;  // after the announcement
        } else if (strncmp(result->screen, "games", 5) == 0) {
            publish_screen(result->screen, NULL);  // the gaming area or one game, drawn by the board
        } else if (strncmp(result->screen, "triqui?", 7) == 0) {
            publish_screen("triqui", NULL);
        } else if (strncmp(result->screen, "music_sleep?s=", 14) == 0) {
            music_sleep_in(atoi(result->screen + 14));
        } else if (strncmp(result->screen, "volume?v=", 9) == 0) {
            volume_set = atoi(result->screen + 9);
        } else if (strncmp(result->screen, "timer?", 6) == 0) {
            // Saved on the board first, so it rings even if the PC is off later
            char id[16] = "", at[24] = "", name[64] = "", repeat[16] = "", kind[8] = "";
            if (param(result->screen, "id", id, sizeof(id)) && param(result->screen, "at", at, sizeof(at))) {
                param(result->screen, "l", name, sizeof(name));
                param(result->screen, "r", repeat, sizeof(repeat));
                param(result->screen, "k", kind, sizeof(kind));
                alarms_add((uint32_t)strtoul(id, NULL, 10), (time_t)atoll(at), name[0] ? name : "Temporizador",
                           (uint32_t)strtoul(repeat, NULL, 10), (uint8_t)atoi(kind));
            }
        } else if (strncmp(result->screen, "timer_cancel?", 13) == 0) {
            char ids[96] = "";
            if (param(result->screen, "ids", ids, sizeof(ids))) {
                alarms_cancel_ids(ids);
            }
        } else if (strcmp(result->screen, "music_local") == 0) {
            // The board chose the station (or the card) itself
            publish_screen("music", local_radio_screen_json());
            strlcpy(music_path, local_radio_url(), sizeof(music_path));
            start_music = true;
        } else if (strcmp(result->screen, "music_stop") == 0) {
            music_stop();
            publish_screen("home", NULL);
        } else if (strcmp(result->screen, "home_reload") == 0) {
            publish_screen("home_reload", NULL);  // the Home screen is built again with the new look
        } else if (strcmp(result->screen, "home") == 0) {
            publish_screen("home", NULL);
        } else if (strncmp(result->screen, "article", 7) == 0 || strncmp(result->screen, "headlines", 9) == 0) {
            // Text answers (jokes, news, Wikipedia, books...) are only spoken: no screen of text to close afterwards
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
    if (clips && clip_count > 0) {
        clips_play(clips, clip_count);
    } else {
        audio_write_samples(result->audio, result->audio_bytes);
    }
    if (pending_voice[0]) {
        char text[420];
        strlcpy(text, pending_voice, sizeof(text));
        pending_voice[0] = '\0';
        char lang[8];
        strlcpy(lang, pending_voice_lang, sizeof(lang));
        pending_voice_lang[0] = '\0';
        tts_say_lang(text, lang[0] ? lang : "es");
    }
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
// Tries to join the saved network, but never holds the board up for long: it works without Wi-Fi, and the network
// manager keeps retrying in the background.
static void wait_for_wifi(void) {
    uint32_t started = pdTICKS_TO_MS(xTaskGetTickCount());
    for (;;) {
        bug_settings_t settings;
        settings_copy(&settings);

        if (!settings.wifi_ssid[0]) {
            ESP_LOGI(TAG, "No Wi-Fi saved yet");
            set_state(ASSISTANT_STATE_NO_CONFIG);
            wifi_dirty = false;
            for (int i = 0; i < 30 && !wifi_dirty; i++) {  // a few seconds for the Wi-Fi list to open, then carry on
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (wifi_dirty) continue;
            return;
        }

        wifi_dirty = false;
        set_state(ASSISTANT_STATE_CONNECTING);
        esp_err_t err = network_manager_connect_wifi(settings.wifi_ssid, settings.wifi_password);
        if (err != ESP_OK && network_manager_last_error() != NET_ERR_NONE) {
            set_state(ASSISTANT_STATE_WIFI_ERROR);
        }
        while (network_manager_is_connected() != ESP_OK && !wifi_dirty &&
               pdTICKS_TO_MS(xTaskGetTickCount()) - started < 15000) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (wifi_dirty) {
            continue;
        }
        return;
    }
}

// The wake word was heard (or the ring was tapped): listen for the command and answer it.
static void speak(const voice_result_t *result) {
    speak_with(result, NULL, 0);
}

// The PC sent only the words. The board answers by itself when it can; otherwise it asks the PC.
static void handle_heard(const char *words) {
    local_answer_t local;
    if (local_skills_try(words, &local)) {
        if (strcmp(local.screen, "article") == 0 || strcmp(local.screen, "headlines") == 0) {
            heap_caps_free(local.json);  // spoken only: the Home screen stays
            local.json = NULL;
            local.screen[0] = '\0';
        }
        voice_result_t made = {.action = VOICE_ACTION_REPLY};
        strlcpy(made.heard, words, sizeof(made.heard));
        strlcpy(made.reply, local.reply, sizeof(made.reply));
        strlcpy(made.screen, local.screen, sizeof(made.screen));
        if (local.json) {
            // The board built this screen's data itself: no need to ask the PC for it
            publish_screen(local.screen, local.json);
            local.json = NULL;
            made.screen[0] = '\0';
        }
        ESP_LOGI(TAG, "Heard \"%s\", answering on the board: \"%s\"", words, local.reply);
        remember_exchange(made.heard, made.reply);
        strlcpy(pending_voice, local.speak, sizeof(pending_voice));
        strlcpy(pending_voice_lang, local.speak_lang, sizeof(pending_voice_lang));
        speak_with(&made, local.clips, local.clip_count);
        return;
    }
    ESP_LOGI(TAG, "Heard \"%s\", asking the PC", words);
    set_texts(words, "");
    set_state(ASSISTANT_STATE_THINKING);
    voice_result_t answer;
    if (!pc_available() || voice_client_ask(words, &answer) != ESP_OK) {
        if (pc_available()) report_unreachable();
        voice_result_t made = {.action = VOICE_ACTION_REPLY};
        strlcpy(made.heard, words, sizeof(made.heard));
        strlcpy(made.reply, "No tengo conexión en este momento.", sizeof(made.reply));
        static const int clips[] = {CLIP_SIN_CONEXION};
        speak_with(&made, clips, 1);
        return;
    }
    if (answer.action == VOICE_ACTION_REPLY) {
        remember_exchange(answer.heard, answer.reply);
        speak(&answer);
    }
    voice_result_free(&answer);
}

// Guided recording: the board asks for one phrase at a time (the PC picks which), records the owner saying it and
// sends the clip, so the models on the board can be trained with the owner's own voice. It stops after three
// prompts with no answer, or when every phrase has enough recordings.
static void run_training(int16_t *utterance) {
    int misses = 0, saved = 0;
    for (;;) {
        char label[32] = "", phrase[96] = "";
        bool done = false, raw = false;
        if (voice_client_train_next(label, sizeof(label), phrase, sizeof(phrase), &done, &raw) != ESP_OK) {
            set_texts("", "No encuentro el PC para guardar tu voz");
            set_state(ASSISTANT_STATE_NO_SERVER);
            vTaskDelay(pdMS_TO_TICKS(3000));
            break;
        }
        if (done) {
            set_texts("", "Terminamos: ya tengo todas tus frases");
            break;
        }
        // The phrase to say is shown as the small gray line; the state changes twice so the screen redraws it
        set_texts(phrase, "");
        set_state(ASSISTANT_STATE_THINKING);
        vTaskDelay(pdMS_TO_TICKS(80));
        set_state(ASSISTANT_STATE_LISTENING);
        audio_set_speaker(true);
        vTaskDelay(pdMS_TO_TICKS(30));
        audio_beep(1175.0f, 70);
        audio_set_speaker(false);
        audio_flush_input(150);

        static int16_t *tap = NULL;  // room for 9 s of both microphones at 32 kHz (external memory), made the first time the PC asks for raw clips
        const size_t tap_samples = 9 * 32000 * 2;
        if (raw && !tap) tap = heap_caps_malloc(tap_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (raw && tap) audio_raw_tap_start(tap, tap_samples);
        size_t samples = capture_utterance(utterance, 7000, ASSISTANT_STATE_LISTENING, false);
        size_t tapped = (raw && tap) ? audio_raw_tap_stop() : 0;
        if (samples == 0) {
            if (++misses >= 3) {
                set_texts("", "No te oí. Seguimos otro día");
                break;
            }
            continue;
        }
        misses = 0;
        if (voice_client_train_clip(label, utterance, samples * sizeof(int16_t)) == ESP_OK) {
            saved++;
        }
        if (tapped > 0) voice_client_train_raw(label, tap, tapped * sizeof(int16_t));  // the same moment, from each microphone, for the audio lab
    }
    ESP_LOGI(TAG, "Training session ended: %d clips saved", saved);
    vTaskDelay(pdMS_TO_TICKS(1500));
    set_texts("", "");
}

static void handle_command_with_voice(int16_t *utterance);

static void handle_command(int16_t *utterance) {
    // Music and Bug's voice share the speaker, so the music waits and then carries on
    music_pause_for_voice();
    handle_command_with_voice(utterance);
    music_resume_after_voice();
}

static void handle_command_with_voice(int16_t *utterance) {
    set_state(ASSISTANT_STATE_LISTENING);  // the ring first, so it appears at once
    {   // "Te escucho": said when the board is really ready to hear the command
        audio_set_speaker(true);
        vTaskDelay(pdMS_TO_TICKS(30));
        static const int ready[] = {CLIP_TE_ESCUCHO};
        clips_play(ready, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        audio_set_speaker(false);
        audio_flush_input(300);  // do not hear our own voice
    }
    (void)skip_chime;
    size_t samples = capture_utterance(utterance, COMMAND_WAIT_MS, ASSISTANT_STATE_LISTENING, false);
    if (samples == 0) {
        ESP_LOGI(TAG, "No command heard, back to standby");
        return;
    }

    // First the board's own recogniser (no PC, no internet); only if it is not sure does the PC listen
    float confidence = 0;
    const char *sentence = command_local_recognize(utterance, samples, &confidence);
    if (sentence) {
        ESP_LOGI(TAG, "Recognised on the board: \"%s\" (%.2f)", sentence, confidence);
        handle_heard(sentence);
        return;
    }

    set_state(ASSISTANT_STATE_THINKING);
    voice_result_t result;
    esp_err_t err = pc_available() ? voice_client_listen(true, utterance, samples * sizeof(int16_t), &result) : ESP_FAIL;
    if (err != ESP_OK) {
        // Neither the board nor the PC understood: say so, briefly (and do not bother a PC that did not answer for a while)
        if (pc_available()) pc_down_until_ms = pdTICKS_TO_MS(xTaskGetTickCount()) + 60000;
        voice_result_t made = {.action = VOICE_ACTION_REPLY};
        strlcpy(made.reply, "No te entendí.", sizeof(made.reply));
        static const int clips[] = {CLIP_NO_ENTENDI};
        speak_with(&made, clips, 1);
        return;
    }
    if (result.action == VOICE_ACTION_HEARD) {
        char words[128];
        strlcpy(words, result.heard, sizeof(words));
        voice_result_free(&result);
        handle_heard(words);
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
    clips_init();

    wait_for_wifi();
    if (network_manager_is_connected() == ESP_OK) {
        server_finder_refresh();  // the PC may have a new address today
    }
    ESP_LOGI(TAG, "Ready (Wi-Fi %s). Server: %s", network_manager_is_connected() == ESP_OK ? "connected" : "not connected", settings.server_url);
    clock_start();
    calibrate_noise();

    for (;;) {
        set_state(muted ? ASSISTANT_STATE_MUTED : (music_is_playing() ? ASSISTANT_STATE_MUSIC : ASSISTANT_STATE_STANDBY));
        if (announce_pending) say_announcement();
        if (test_words_pending) {
            char words[200];
            strlcpy(words, test_words, sizeof(words));
            test_words_pending = false;
            ESP_LOGI(TAG, "Test phrase: \"%s\"", words);
            handle_heard(words);
        }
        // When the PC does not answer, look for it by name every 30 seconds: its address may have changed
        static uint32_t last_find_ms = 0;
        uint32_t clock_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        if (!pc_available() && network_manager_is_connected() == ESP_OK && clock_ms - last_find_ms > 30000) {
            last_find_ms = clock_ms;
            if (server_finder_refresh()) {
                pc_down_until_ms = 0;
            }
        }
        if (wifi_dirty) {  // the owner chose another network
            wifi_dirty = false;
            bug_settings_t wifi_settings;
            settings_copy(&wifi_settings);
            if (wifi_settings.wifi_ssid[0]) {
                network_manager_connect_wifi(wifi_settings.wifi_ssid, wifi_settings.wifi_password);
            }
        }
        // Sounds in standby are only checked quietly: the screen reacts once "Hola Bug" is confirmed
        if (train_requested) {
            train_requested = false;
            run_training(utterance);
            continue;
        }
        local_wake = false;
        wake_local_reset();
        wake_listening = false;  // the streaming detector is off: the sentence recogniser below decides
        size_t samples = capture_utterance(utterance, 0, current_state, true);
        wake_listening = false;

        if (local_wake) {
            // The board itself recognised "Hola Bug": go straight to listening, without the PC's wake check and
            // without a beep (a beep would be picked up as the start of the command)
            local_wake = false;
            if (!muted) {
                ESP_LOGI(TAG, "Wake word heard on the board");
                set_texts("", "");
                skip_chime = true;
                handle_command(utterance);
                skip_chime = false;
            }
            wake_local_reset();
            continue;
        }

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

        // The board's own recogniser first: was that "Hola Bug"? (no PC, no internet needed)
        {
            float wake_confidence = 0;
            const char *heard_id = command_local_classify(utterance, samples, &wake_confidence);
            bool recognised = heard_id && strcmp(heard_id, "wake") == 0 && wake_confidence >= 0.65f;
            if (!recognised && samples >= 6400) {
                // A second opinion from the board's own greeting check, before the PC is asked (0 in memory turns it off)
                float needed = wake_verify_threshold();
                if (needed > 0) {
                    float greeting = wake_verify_score(utterance, samples);
                    if (greeting >= needed) {
                        recognised = true;
                        wake_confidence = greeting;
                        ESP_LOGI(TAG, "Greeting accepted by the board's check (%.2f)", greeting);
                    }
                }
            }
            if (recognised) {
                ESP_LOGI(TAG, "Wake word recognised on the board (%.2f)", wake_confidence);
                if (!muted) {
                    // "Hola Bug, qué hora es" in one breath: look for the command in the rest of the sentence
                    const char *sentence = NULL;
                    static const size_t offsets[] = {11000, 16000};
                    for (int k = 0; k < 2 && !sentence; k++) {
                        if (samples > offsets[k] + 6000) {
                            float c2 = 0;
                            sentence = command_local_recognize(utterance + offsets[k], samples - offsets[k], &c2);
                        }
                    }
                    if (sentence) {
                        ESP_LOGI(TAG, "...and the command in the same sentence: \"%s\"", sentence);
                        handle_heard(sentence);
                    } else {
                        set_texts("", "");
                        skip_chime = true;
                        handle_command(utterance);
                        skip_chime = false;
                    }
                }
                continue;
            }
        }
        if (!pc_available()) {  // nothing the board could recognise, and the PC is known to be down
            set_texts("", "");
            continue;
        }

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
        } else if (result.action == VOICE_ACTION_HEARD) {
            char words[128];
            strlcpy(words, result.heard, sizeof(words));
            voice_result_free(&result);
            handle_heard(words);
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
    xTaskCreatePinnedToCore(assistant_task, "assistant", 12288, NULL, 5, NULL, 1);
}

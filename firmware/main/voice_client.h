#ifndef VOICE_CLIENT_H
#define VOICE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    VOICE_ACTION_ERROR,   // No usable answer from the server
    VOICE_ACTION_IGNORE,  // Not the wake word, or nothing heard
    VOICE_ACTION_WAKE,    // Wake word heard with no command yet
    VOICE_ACTION_REPLY,   // Spoken answer in `audio`
    VOICE_ACTION_HEARD,   // Only the words (in `heard`): the board decides what to do with them
} voice_action_t;

typedef struct {
    voice_action_t action;
    char heard[128];      // What the server understood (UTF-8). Empty when ignored.
    char reply[256];      // The answer as text (UTF-8)
    char screen[160];     // Screen or action with the answer, e.g. "email", "email_detail?i=0" or "timer?id=3&at=...".
    int16_t *audio;       // 16 kHz mono PCM, in PSRAM. Release with voice_result_free().
    size_t audio_bytes;
} voice_result_t;

// Sends one recorded sentence (16 kHz, 16-bit mono) to the PC server.
// command_mode=false waits for "Hola Bug"/"Hey Bug"; true treats the sentence as the command.
esp_err_t voice_client_listen(bool command_mode, const int16_t *pcm, size_t pcm_bytes, voice_result_t *result);
void voice_result_free(voice_result_t *result);

// The board understood the words (text, UTF-8) but cannot answer them itself: the PC answers, with its voice.
esp_err_t voice_client_ask(const char *text, voice_result_t *result);

// Turns "Llamar%20a%20Ana" back into text. Used for header values and for values inside them.
void voice_client_percent_decode(char *dst, size_t cap, const char *src);

// Downloads the data a skill screen draws, as JSON text. Release with heap_caps_free().
// Logs how fast the Wi-Fi link to the server is, both ways (a diagnostic, takes about a second).
void voice_client_bench(void);

// Guided recording of the owner's voice (training data for the models on the board).
// Asks the PC which phrase to record next. `done` is true when every phrase has enough recordings.
esp_err_t voice_client_train_next(char *label, size_t label_size, char *text, size_t text_size, bool *done);
// Uploads one recording (16 kHz, 16-bit mono) under the phrase's label.
esp_err_t voice_client_train_clip(const char *label, const int16_t *pcm, size_t pcm_bytes);

esp_err_t voice_client_fetch_screen(const char *path, char **json);

#endif

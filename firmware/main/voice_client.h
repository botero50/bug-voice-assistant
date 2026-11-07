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
} voice_action_t;

typedef struct {
    voice_action_t action;
    int16_t *audio;       // 16 kHz mono PCM, in PSRAM. Release with voice_result_free().
    size_t audio_bytes;
} voice_result_t;

// Sends one recorded sentence (16 kHz, 16-bit mono) to the PC server.
// command_mode=false waits for "Hola Bug"/"Hey Bug"; true treats the sentence as the command.
esp_err_t voice_client_listen(bool command_mode, const int16_t *pcm, size_t pcm_bytes, voice_result_t *result);
void voice_result_free(voice_result_t *result);

#endif

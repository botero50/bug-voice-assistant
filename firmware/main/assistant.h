#ifndef ASSISTANT_H
#define ASSISTANT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ASSISTANT_STATE_NO_CONFIG,   // Wi-Fi name not set in bug_config.local.h
    ASSISTANT_STATE_CONNECTING,  // Joining Wi-Fi
    ASSISTANT_STATE_STANDBY,     // Waiting for "Hola Bug" / "Hey Bug"
    ASSISTANT_STATE_HEARING,     // Someone started talking; recording it
    ASSISTANT_STATE_LISTENING,   // Heard the wake word, waiting for the command
    ASSISTANT_STATE_THINKING,    // Sent to the PC, waiting for the answer
    ASSISTANT_STATE_SPEAKING,    // Playing the answer
    ASSISTANT_STATE_NO_SERVER,   // The PC server didn't answer
} assistant_state_t;

// Starts the standby / wake word / command / answer loop in its own task.
// Call after the audio driver and network manager are initialized.
void assistant_start(void);

// Returns true once per state change, so the main loop can update the screen.
bool assistant_take_state(assistant_state_t *state);

// Latest microphone level from 0 to 1, for the waveform.
float assistant_audio_level(void);

// What the server last understood and answered (UTF-8). Valid when a state change is reported.
void assistant_get_texts(char *heard, size_t heard_size, char *reply, size_t reply_size);

#endif

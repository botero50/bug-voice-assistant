#ifndef ASSISTANT_H
#define ASSISTANT_H

#include <stdbool.h>

typedef enum {
    ASSISTANT_STATE_NO_CONFIG,   // Wi-Fi name not set in bug_config.local.h
    ASSISTANT_STATE_CONNECTING,  // Joining Wi-Fi
    ASSISTANT_STATE_STANDBY,     // Waiting for "Hola Bug" / "Hey Bug"
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

#endif

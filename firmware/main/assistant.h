#ifndef ASSISTANT_H
#define ASSISTANT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ASSISTANT_STATE_NO_CONFIG,   // No Wi-Fi saved yet
    ASSISTANT_STATE_CONNECTING,  // Joining Wi-Fi
    ASSISTANT_STATE_WIFI_ERROR,  // Saved Wi-Fi didn't work (wrong password or out of range)
    ASSISTANT_STATE_STANDBY,     // Waiting for "Hola Bug" / "Hey Bug"
    ASSISTANT_STATE_MUTED,       // Microphone switched off
    ASSISTANT_STATE_MUSIC,       // Radio playing; the mic ignores speech until the ring is tapped
    ASSISTANT_STATE_HEARING,     // Someone started talking; recording it
    ASSISTANT_STATE_LISTENING,   // Heard the wake word, waiting for the command
    ASSISTANT_STATE_THINKING,    // Sent to the PC, waiting for the answer
    ASSISTANT_STATE_SPEAKING,    // Playing the answer
    ASSISTANT_STATE_NO_SERVER,   // The PC server didn't answer
    ASSISTANT_STATE_MIC_ERROR,   // The microphone stopped delivering audio
} assistant_state_t;

#define ASSISTANT_HISTORY_SIZE 5

// Starts the standby / wake word / command / answer loop in its own task.
// Call after the audio driver, network manager and settings are initialized.
void assistant_start(void);

// Returns true once per state change, so the main loop can update the screen.
bool assistant_take_state(assistant_state_t *state);

// Latest microphone level from 0 to 1, for the waveform.
float assistant_audio_level(void);

// What the server last understood and answered (UTF-8). Valid when a state change is reported.
void assistant_get_texts(char *heard, size_t heard_size, char *reply, size_t reply_size);

// True when the last answer comes with a screen to open (see assistant_take_screen).
bool assistant_screen_pending(void);

// Takes the screen the last answer asked for: its path ("email", "calendar", "home"...) and its
// JSON data (NULL for "home"). The caller frees the JSON with heap_caps_free().
bool assistant_take_screen(char *path, size_t path_size, char **json);

// Call after the Wi-Fi settings changed so a waiting assistant tries again.
void assistant_wifi_changed(void);
void assistant_announce(const char *text);  // say a message from the development PC when idle
void assistant_open_screen(const char *path);

// Skips the wake word and listens for a command right now (tap on the ring).
void assistant_listen_now(void);

// Switches the microphone off or on.
void assistant_set_muted(bool muted);

// Last exchanges, newest first (index 0). Returns false past the end.
bool assistant_history_get(int index, char *heard, size_t heard_size, char *reply, size_t reply_size);

#endif

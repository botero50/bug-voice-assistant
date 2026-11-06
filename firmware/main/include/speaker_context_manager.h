#ifndef SPEAKER_CONTEXT_MANAGER_H
#define SPEAKER_CONTEXT_MANAGER_H

#include <stdint.h>
#include <esp_err.h>
#include <time.h>
#include "speaker_identification.h"

typedef enum {
    VOICE_SPEED_SLOW = 0,
    VOICE_SPEED_NORMAL = 1,
    VOICE_SPEED_FAST = 2,
} VoiceSpeed;

typedef enum {
    RESPONSE_LENGTH_SHORT = 0,
    RESPONSE_LENGTH_MEDIUM = 1,
    RESPONSE_LENGTH_LONG = 2,
} ResponseLength;

typedef struct {
    char speaker_id[16];
    char speaker_name[32];
    char language[8];
    char timezone[32];
    char email_account[64];
    char calendar_account[64];
    VoiceSpeed voice_speed;
    ResponseLength response_length;
    int use_text_display;
    int is_authenticated;
    uint32_t load_time;
} SpeakerContext;

// Context lifecycle
esp_err_t speaker_context_manager_load_context(const SpeakerProfile *profile);
esp_err_t speaker_context_manager_clear_context(void);
const SpeakerContext *speaker_context_manager_get_context(void);

// Quick accessors
const char *speaker_context_manager_get_speaker_id(void);
const char *speaker_context_manager_get_speaker_name(void);
const char *speaker_context_manager_get_email_account(void);
const char *speaker_context_manager_get_calendar_account(void);
const char *speaker_context_manager_get_language(void);
const char *speaker_context_manager_get_timezone(void);
int speaker_context_manager_is_authenticated(void);

// Preferences
esp_err_t speaker_context_manager_set_preference(const char *key, const char *value);

// Debugging
void speaker_context_manager_print_context(void);

#endif

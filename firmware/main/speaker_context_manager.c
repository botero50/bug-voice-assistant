#include "speaker_context_manager.h"
#include <string.h>
#include <esp_log.h>

static const char *TAG = "SPEAKER_CTX";

static SpeakerContext current_context = {0};

esp_err_t speaker_context_manager_load_context(const SpeakerProfile *profile) {
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }

    // Load user context from profile
    strcpy(current_context.speaker_id, profile->speaker_id);
    strcpy(current_context.speaker_name, profile->name);
    strcpy(current_context.language, profile->language);
    strcpy(current_context.timezone, profile->timezone);

    // Set default preferences (can be enhanced with stored user settings)
    current_context.voice_speed = VOICE_SPEED_NORMAL;
    current_context.response_length = RESPONSE_LENGTH_MEDIUM;
    current_context.use_text_display = 1;

    // Load user-specific accounts (simplified - in real implementation, load from storage)
    strcpy(current_context.email_account, profile->speaker_id);  // Use speaker_id as account key
    strcpy(current_context.calendar_account, profile->speaker_id);

    current_context.is_authenticated = 1;
    current_context.load_time = (uint32_t)time(NULL);

    ESP_LOGI(TAG, "Context loaded for %s (ID: %s)", profile->name, profile->speaker_id);
    ESP_LOGI(TAG, "  Language: %s, Timezone: %s", current_context.language,
             current_context.timezone);

    return ESP_OK;
}

esp_err_t speaker_context_manager_clear_context(void) {
    memset(&current_context, 0, sizeof(SpeakerContext));
    current_context.is_authenticated = 0;

    ESP_LOGI(TAG, "Context cleared");
    return ESP_OK;
}

const SpeakerContext *speaker_context_manager_get_context(void) {
    if (!current_context.is_authenticated) {
        return NULL;
    }
    return &current_context;
}

const char *speaker_context_manager_get_speaker_id(void) {
    return current_context.is_authenticated ? current_context.speaker_id : "UNKNOWN";
}

const char *speaker_context_manager_get_speaker_name(void) {
    return current_context.is_authenticated ? current_context.speaker_name : "Unknown";
}

const char *speaker_context_manager_get_email_account(void) {
    return current_context.is_authenticated ? current_context.email_account : NULL;
}

const char *speaker_context_manager_get_calendar_account(void) {
    return current_context.is_authenticated ? current_context.calendar_account : NULL;
}

const char *speaker_context_manager_get_language(void) {
    return current_context.is_authenticated ? current_context.language : "es-CO";
}

const char *speaker_context_manager_get_timezone(void) {
    return current_context.is_authenticated ? current_context.timezone : "America/Bogota";
}

int speaker_context_manager_is_authenticated(void) {
    return current_context.is_authenticated;
}

esp_err_t speaker_context_manager_set_preference(const char *key, const char *value) {
    if (!current_context.is_authenticated) {
        return ESP_ERR_INVALID_STATE;
    }

    if (strcmp(key, "voice_speed") == 0) {
        if (strcmp(value, "slow") == 0) {
            current_context.voice_speed = VOICE_SPEED_SLOW;
        } else if (strcmp(value, "normal") == 0) {
            current_context.voice_speed = VOICE_SPEED_NORMAL;
        } else if (strcmp(value, "fast") == 0) {
            current_context.voice_speed = VOICE_SPEED_FAST;
        }
    } else if (strcmp(key, "response_length") == 0) {
        if (strcmp(value, "short") == 0) {
            current_context.response_length = RESPONSE_LENGTH_SHORT;
        } else if (strcmp(value, "medium") == 0) {
            current_context.response_length = RESPONSE_LENGTH_MEDIUM;
        } else if (strcmp(value, "long") == 0) {
            current_context.response_length = RESPONSE_LENGTH_LONG;
        }
    }

    ESP_LOGI(TAG, "Preference set: %s = %s", key, value);
    return ESP_OK;
}

void speaker_context_manager_print_context(void) {
    if (!current_context.is_authenticated) {
        ESP_LOGI(TAG, "No authenticated context");
        return;
    }

    ESP_LOGI(TAG, "\n╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Current Speaker Context                    ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Name: %s", current_context.speaker_name);
    ESP_LOGI(TAG, "║ ID: %s", current_context.speaker_id);
    ESP_LOGI(TAG, "║ Language: %s", current_context.language);
    ESP_LOGI(TAG, "║ Timezone: %s", current_context.timezone);
    ESP_LOGI(TAG, "║ Email Account: %s", current_context.email_account);
    ESP_LOGI(TAG, "║ Calendar: %s", current_context.calendar_account);
    ESP_LOGI(TAG, "║ Voice Speed: %d (0=slow, 1=normal, 2=fast)", current_context.voice_speed);
    ESP_LOGI(TAG, "║ Response Length: %d (0=short, 1=medium, 2=long)",
             current_context.response_length);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝\n");
}

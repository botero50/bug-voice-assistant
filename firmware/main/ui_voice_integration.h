#ifndef UI_VOICE_INTEGRATION_H
#define UI_VOICE_INTEGRATION_H

#include "ui_config.h"

// Voice event callbacks - update UI based on voice assistant state
void ui_on_wake_word_detected(void);
void ui_on_stt_start(void);
void ui_on_stt_update(const char *partial_transcription, const uint16_t *left_audio, const uint16_t *right_audio, uint32_t audio_samples);
void ui_on_stt_complete(const char *full_transcription);
void ui_on_llm_start(void);
void ui_on_llm_progress(const char *status_text);
void ui_on_llm_complete(const char *response);
void ui_on_tts_start(const char *response_text);
void ui_on_tts_progress(float progress_percent);
void ui_on_tts_complete(void);
void ui_on_voice_error(const char *error_message);
void ui_on_voice_idle(void);

// Voice skill callbacks
void ui_on_skill_email_list_update(const char *emails[], int count);
void ui_on_skill_calendar_update(const char *events[], int count);
void ui_on_skill_joke_update(const char *setup, const char *punchline);
void ui_on_skill_radio_update(const char *station, const char *now_playing);
void ui_on_skill_crypto_update(const char *btc_price, const char *eth_price);
void ui_on_skill_book_update(const char *title, const char *chapter, int page, int total);
void ui_on_skill_story_update(const char *story_text);

// Battery and system status callbacks
void ui_on_battery_update(uint8_t percent);
void ui_on_wifi_status_change(bool connected);
void ui_on_mic_mute_change(bool muted);
void ui_on_power_profile_change(power_profile_t profile);

#endif // UI_VOICE_INTEGRATION_H

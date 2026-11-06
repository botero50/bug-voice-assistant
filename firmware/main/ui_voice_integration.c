#include "ui_voice_integration.h"
#include "ui_core.h"
#include "ui_voice_feedback.h"
#include "ui_email.h"
#include "ui_calendar.h"
#include "ui_skills.h"
#include "display_driver.h"
#include "esp_log.h"

static const char *TAG = "UI_VOICE_INT";

// ==================== VOICE EVENT CALLBACKS ====================

void ui_on_wake_word_detected(void) {
    ESP_LOGI(TAG, "Wake word detected → showing listening state");
    ui_set_voice_state(VOICE_STATE_LISTENING);
}

void ui_on_stt_start(void) {
    ESP_LOGI(TAG, "STT started → showing waveform");
    ui_set_screen(SCREEN_VOICE_FEEDBACK);
    ui_create_voice_feedback_stt("Listening...");
    ui_set_voice_state(VOICE_STATE_STT);
}

void ui_on_stt_update(const char *partial_transcription, const uint16_t *left_audio, const uint16_t *right_audio, uint32_t audio_samples) {
    // Update waveform visualization and transcription text
    if (left_audio && right_audio) {
        ui_update_waveform(left_audio, right_audio, audio_samples);
    }
    if (partial_transcription) {
        ui_update_voice_text(partial_transcription);
    }
}

void ui_on_stt_complete(const char *full_transcription) {
    ESP_LOGI(TAG, "STT complete: %s", full_transcription ? full_transcription : "");
    // Keep transcription visible, prepare for LLM
}

void ui_on_llm_start(void) {
    ESP_LOGI(TAG, "LLM processing started → showing spinner");
    ui_create_voice_feedback_llm();
    ui_set_voice_state(VOICE_STATE_LLM);
}

void ui_on_llm_progress(const char *status_text) {
    // Update status text during processing
    if (status_text) {
        ui_update_voice_text(status_text);
    }
}

void ui_on_llm_complete(const char *response) {
    ESP_LOGI(TAG, "LLM response ready, starting TTS: %s", response ? response : "");
    // Transition to TTS screen
}

void ui_on_tts_start(const char *response_text) {
    ESP_LOGI(TAG, "TTS started → showing response with audio ring");
    ui_create_voice_feedback_tts(response_text);
    ui_set_voice_state(VOICE_STATE_TTS);
}

void ui_on_tts_progress(float progress_percent) {
    // Update text scroll position based on playback progress
    // Auto-scroll response text synchronized with audio playback
    ESP_LOGD(TAG, "TTS progress: %.1f%%", progress_percent);
}

void ui_on_tts_complete(void) {
    ESP_LOGI(TAG, "TTS complete → returning to home");
    ui_set_screen(SCREEN_HOME);
    ui_set_voice_state(VOICE_STATE_IDLE);
}

void ui_on_voice_error(const char *error_message) {
    ESP_LOGE(TAG, "Voice error: %s", error_message ? error_message : "Unknown");
    ui_set_voice_state(VOICE_STATE_ERROR);
    // Show error screen with "Try again" option
    // Auto-dismiss after 3 seconds or on touch
    // Voice: "What was that? Try again"
}

void ui_on_voice_idle(void) {
    ESP_LOGI(TAG, "Voice assistant idle → returning to home");
    ui_set_screen(SCREEN_HOME);
    ui_set_voice_state(VOICE_STATE_IDLE);
}

// ==================== SKILL CALLBACKS ====================

void ui_on_skill_email_list_update(const char *emails[], int count) {
    ESP_LOGI(TAG, "Email list updated: %d emails", count);
    ui_set_screen(SCREEN_EMAIL_LIST);
    // TODO: Pass actual email data to list screen
}

void ui_on_skill_calendar_update(const char *events[], int count) {
    ESP_LOGI(TAG, "Calendar updated: %d events", count);
    ui_set_screen(SCREEN_CALENDAR);
    // TODO: Pass actual event data to calendar screen
}

void ui_on_skill_joke_update(const char *setup, const char *punchline) {
    ESP_LOGI(TAG, "Joke: %s", setup ? setup : "");
    ui_set_screen(SCREEN_HOME);  // Temporarily use home screen
    ui_create_jokes_screen(setup, punchline);
}

void ui_on_skill_radio_update(const char *station, const char *now_playing) {
    ESP_LOGI(TAG, "Radio: %s - %s", station ? station : "", now_playing ? now_playing : "");
    ui_create_radio_screen(station, now_playing);
}

void ui_on_skill_crypto_update(const char *btc_price, const char *eth_price) {
    ESP_LOGI(TAG, "Crypto: BTC=%s, ETH=%s", btc_price ? btc_price : "", eth_price ? eth_price : "");
    ui_create_crypto_screen(btc_price, eth_price);
}

void ui_on_skill_book_update(const char *title, const char *chapter, int page, int total) {
    ESP_LOGI(TAG, "Book: %s, Chapter: %s, Page: %d/%d", title ? title : "", chapter ? chapter : "", page, total);
    ui_create_book_reader_screen(title, chapter, page, total);
}

void ui_on_skill_story_update(const char *story_text) {
    ESP_LOGI(TAG, "Story generated");
    ui_create_story_generator_screen(story_text);
}

// ==================== SYSTEM STATUS CALLBACKS ====================

void ui_on_battery_update(uint8_t percent) {
    ui_update_battery(percent);
    // Color-code battery: green >50%, yellow 25-50%, red <25%
    // Auto-switch power profiles if battery critical
    if (percent < 5) {
        ESP_LOGW(TAG, "CRITICAL BATTERY: %.0d%% - Switch to Gesture-only mode", percent);
        // Trigger power profile switch to GESTURE_ONLY
    }
}

void ui_on_wifi_status_change(bool connected) {
    ui_update_wifi_status(connected);
    ESP_LOGI(TAG, "WiFi %s", connected ? "connected" : "disconnected");
}

void ui_on_mic_mute_change(bool muted) {
    ui_mute_mic(muted);
    ESP_LOGI(TAG, "Microphone %s", muted ? "muted" : "unmuted");
}

void ui_on_power_profile_change(power_profile_t profile) {
    ESP_LOGI(TAG, "Power profile changed: %d", profile);
    // Update display indicator (e.g., ring color changes based on profile)
    // Always-Listening: full brightness
    // Periodic: dimmed
    // Gesture-Only: display off, touch wakes
}

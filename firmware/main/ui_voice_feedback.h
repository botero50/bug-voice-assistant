#ifndef UI_VOICE_FEEDBACK_H
#define UI_VOICE_FEEDBACK_H

#include "lvgl.h"

// Voice feedback screen creation (one at a time)
lv_obj_t *ui_create_voice_feedback_stt(const char *transcription);  // Speech-to-text with waveform
lv_obj_t *ui_create_voice_feedback_llm(void);                       // LLM processing with spinner
lv_obj_t *ui_create_voice_feedback_tts(const char *response);       // TTS output with text scroll

// Real-time updates during voice feedback
void ui_update_waveform(const uint16_t *left_data, const uint16_t *right_data, uint32_t count);
void ui_update_voice_text(const char *text);
void ui_rotate_spinner(int32_t angle);

#endif // UI_VOICE_FEEDBACK_H

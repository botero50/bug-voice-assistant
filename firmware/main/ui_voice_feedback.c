#include "ui_voice_feedback.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_VOICE_FEEDBACK";

// Voice feedback screen context
static struct {
    lv_obj_t *container;
    lv_obj_t *ring;
    lv_obj_t *waveform_chart;
    lv_obj_t *status_label;
    lv_obj_t *text_label;
    lv_chart_series_t *chart_series_left;
    lv_chart_series_t *chart_series_right;
} voice_feedback_ctx = {0};

lv_obj_t *ui_create_voice_feedback_stt(const char *transcription) {
    ESP_LOGI(TAG, "Creating STT voice feedback screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    voice_feedback_ctx.container = lv_obj_create(root);
    lv_obj_set_size(voice_feedback_ctx.container, 380, 350);
    lv_obj_set_pos(voice_feedback_ctx.container, 43, 50);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.container, LV_OPA_0, 0);

    // Listening ring (steady green, no pulse)
    voice_feedback_ctx.ring = lv_obj_create(voice_feedback_ctx.container);
    lv_obj_set_size(voice_feedback_ctx.ring, 120, 120);
    lv_obj_align(voice_feedback_ctx.ring, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_radius(voice_feedback_ctx.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(voice_feedback_ctx.ring, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.ring, LV_OPA_0, 0);

    // Recording indicator
    lv_obj_t *recording_label = lv_label_create(voice_feedback_ctx.ring);
    lv_label_set_text(recording_label, "◆");
    lv_obj_align(recording_label, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_text_color(recording_label, COLOR_ACCENT_WHITE, 0);
    lv_obj_set_style_text_font(recording_label, &lv_font_montserrat_20, 0);

    // Waveform visualization (simple bar chart)
    voice_feedback_ctx.waveform_chart = lv_chart_create(voice_feedback_ctx.container);
    lv_obj_set_size(voice_feedback_ctx.waveform_chart, 280, 80);
    lv_obj_align(voice_feedback_ctx.waveform_chart, LV_ALIGN_TOP_MID, 0, 40);
    lv_chart_set_type(voice_feedback_ctx.waveform_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(voice_feedback_ctx.waveform_chart, 10);
    lv_chart_set_axis_tick(voice_feedback_ctx.waveform_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 0);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.waveform_chart, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.waveform_chart, LV_OPA_0, 0);

    voice_feedback_ctx.chart_series_left = lv_chart_add_series(
        voice_feedback_ctx.waveform_chart, COLOR_ACCENT_GREEN, LV_CHART_AXIS_PRIMARY_Y
    );
    voice_feedback_ctx.chart_series_right = lv_chart_add_series(
        voice_feedback_ctx.waveform_chart, COLOR_ACCENT_BLUE, LV_CHART_AXIS_PRIMARY_Y
    );

    // Initialize with random waveform data
    for (int i = 0; i < 10; i++) {
        lv_chart_set_next_value(voice_feedback_ctx.waveform_chart, voice_feedback_ctx.chart_series_left, 30 + (i * 8));
        lv_chart_set_next_value(voice_feedback_ctx.waveform_chart, voice_feedback_ctx.chart_series_right, 20 + (i * 7));
    }

    // Status text ("I hear you...")
    voice_feedback_ctx.status_label = lv_label_create(voice_feedback_ctx.container);
    lv_label_set_text(voice_feedback_ctx.status_label, "I hear you...");
    lv_obj_align(voice_feedback_ctx.status_label, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_text_color(voice_feedback_ctx.status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(voice_feedback_ctx.status_label, &lv_font_montserrat_16, 0);

    // Transcription preview (max 2 lines)
    voice_feedback_ctx.text_label = lv_label_create(voice_feedback_ctx.container);
    lv_label_set_text(voice_feedback_ctx.text_label, transcription ? transcription : "Listening...");
    lv_obj_set_width(voice_feedback_ctx.text_label, 320);
    lv_label_set_long_mode(voice_feedback_ctx.text_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(voice_feedback_ctx.text_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(voice_feedback_ctx.text_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(voice_feedback_ctx.text_label, &lv_font_montserrat_12, 0);

    ESP_LOGI(TAG, "STT voice feedback screen created");

    return voice_feedback_ctx.container;
}

lv_obj_t *ui_create_voice_feedback_llm(void) {
    ESP_LOGI(TAG, "Creating LLM processing screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    voice_feedback_ctx.container = lv_obj_create(root);
    lv_obj_set_size(voice_feedback_ctx.container, 380, 350);
    lv_obj_set_pos(voice_feedback_ctx.container, 43, 50);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.container, LV_OPA_0, 0);

    // Processing ring (rotating blue)
    voice_feedback_ctx.ring = lv_obj_create(voice_feedback_ctx.container);
    lv_obj_set_size(voice_feedback_ctx.ring, 120, 120);
    lv_obj_align(voice_feedback_ctx.ring, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_radius(voice_feedback_ctx.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(voice_feedback_ctx.ring, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.ring, LV_OPA_0, 0);

    // Spinner text
    lv_obj_t *spinner_label = lv_label_create(voice_feedback_ctx.ring);
    lv_label_set_text(spinner_label, "⟳");
    lv_obj_align(spinner_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(spinner_label, COLOR_ACCENT_WHITE, 0);
    lv_obj_set_style_text_font(spinner_label, &lv_font_montserrat_28, 0);

    // Status text
    voice_feedback_ctx.status_label = lv_label_create(voice_feedback_ctx.container);
    lv_label_set_text(voice_feedback_ctx.status_label, "Thinking...");
    lv_obj_align(voice_feedback_ctx.status_label, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_text_color(voice_feedback_ctx.status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(voice_feedback_ctx.status_label, &lv_font_montserrat_16, 0);

    // Add rotation animation to spinner (handled in voice_loop.c)

    ESP_LOGI(TAG, "LLM processing screen created");

    return voice_feedback_ctx.container;
}

lv_obj_t *ui_create_voice_feedback_tts(const char *response) {
    ESP_LOGI(TAG, "Creating TTS output screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    voice_feedback_ctx.container = lv_obj_create(root);
    lv_obj_set_size(voice_feedback_ctx.container, 380, 350);
    lv_obj_set_pos(voice_feedback_ctx.container, 43, 50);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.container, LV_OPA_0, 0);

    // Speaking ring (glowing white)
    voice_feedback_ctx.ring = lv_obj_create(voice_feedback_ctx.container);
    lv_obj_set_size(voice_feedback_ctx.ring, 120, 120);
    lv_obj_align(voice_feedback_ctx.ring, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_radius(voice_feedback_ctx.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(voice_feedback_ctx.ring, COLOR_ACCENT_WHITE, 0);
    lv_obj_set_style_bg_opa(voice_feedback_ctx.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(voice_feedback_ctx.ring, LV_OPA_0, 0);

    // Speaker icon
    lv_obj_t *speaker_label = lv_label_create(voice_feedback_ctx.ring);
    lv_label_set_text(speaker_label, "♪");
    lv_obj_align(speaker_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(speaker_label, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_font(speaker_label, &lv_font_montserrat_28, 0);

    // Response text (auto-scrolls with TTS playback)
    voice_feedback_ctx.text_label = lv_label_create(voice_feedback_ctx.container);
    lv_label_set_text(voice_feedback_ctx.text_label, response ? response : "Speaking...");
    lv_obj_set_width(voice_feedback_ctx.text_label, 340);
    lv_label_set_long_mode(voice_feedback_ctx.text_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(voice_feedback_ctx.text_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(voice_feedback_ctx.text_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(voice_feedback_ctx.text_label, &lv_font_montserrat_14, 0);

    ESP_LOGI(TAG, "TTS output screen created");

    return voice_feedback_ctx.container;
}

void ui_update_waveform(const uint16_t *left_data, const uint16_t *right_data, uint32_t count) {
    if (!voice_feedback_ctx.waveform_chart || count == 0) {
        return;
    }

    // Update waveform chart with new audio data
    for (uint32_t i = 0; i < count && i < 10; i++) {
        lv_chart_set_next_value(
            voice_feedback_ctx.waveform_chart,
            voice_feedback_ctx.chart_series_left,
            left_data[i] > 100 ? 100 : left_data[i]
        );
        lv_chart_set_next_value(
            voice_feedback_ctx.waveform_chart,
            voice_feedback_ctx.chart_series_right,
            right_data[i] > 100 ? 100 : right_data[i]
        );
    }
}

void ui_update_voice_text(const char *text) {
    if (voice_feedback_ctx.text_label) {
        lv_label_set_text(voice_feedback_ctx.text_label, text);
    }
}

void ui_rotate_spinner(int32_t angle) {
    if (voice_feedback_ctx.ring) {
        lv_obj_set_style_transform_angle(voice_feedback_ctx.ring, angle, 0);
    }
}

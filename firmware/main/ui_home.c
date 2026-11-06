#include "ui_home.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_HOME";

// Animation variables
static lv_anim_t anim_pulse;
static lv_anim_t anim_spin;

// Callback for pulse animation (listening state)
static void anim_pulse_callback(void *var, int32_t v) {
    lv_obj_t *ring = (lv_obj_t *)var;
    // Opacity ranges from 60% to 100% in pulse
    uint8_t opa = 60 + (v * 40) / 100;
    lv_obj_set_style_opa(ring, opa, 0);
}

// Callback for spin animation (processing state)
static void anim_spin_callback(void *var, int32_t v) {
    lv_obj_t *ring = (lv_obj_t *)var;
    int16_t rotation = (v * 360) / 100;
    lv_img_set_angle(ring, rotation);
}

lv_obj_t *ui_create_home_screen(void) {
    ESP_LOGI(TAG, "Creating home screen");

    // Get root screen
    lv_obj_t *root = lv_scr_act();

    // Main container (centered, safe area)
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 380, 380);
    lv_obj_set_pos(container, 43, 50);  // Centered (466-380)/2 + 30 for status bar
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);  // Transparent
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Listening ring (outer circle)
    // Outer ring: 160px diameter
    lv_obj_t *ring_outer = lv_obj_create(container);
    lv_obj_set_size(ring_outer, 160, 160);
    lv_obj_align(ring_outer, LV_ALIGN_CENTER, 0, -40);  // Center with slight upward bias
    lv_obj_set_style_radius(ring_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_outer, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_bg_opa(ring_outer, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(ring_outer, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(ring_outer, 0, 0);

    // Inner circle (darker, represents speaker/mic)
    lv_obj_t *ring_inner = lv_obj_create(ring_outer);
    lv_obj_set_size(ring_inner, 110, 110);
    lv_obj_align(ring_inner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(ring_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_inner, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(ring_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(ring_inner, LV_OPA_0, 0);

    // Microphone icon/text in center
    lv_obj_t *mic_label = lv_label_create(ring_inner);
    lv_label_set_text(mic_label, LV_SYMBOL_AUDIO);
    lv_obj_align(mic_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(mic_label, &lv_font_montserrat_28, 0);

    // Listening status text (below ring)
    lv_obj_t *status_label = lv_label_create(container);
    lv_label_set_text(status_label, "Bug preview");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_text_color(status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);

    // Navigation dots (bottom)
    // Layout: [◀] [◉ Home] [▶]
    lv_obj_t *nav_container = lv_obj_create(container);
    lv_obj_set_size(nav_container, 140, 40);
    lv_obj_align(nav_container, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(nav_container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(nav_container, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(nav_container, 0, 0);
    lv_obj_clear_flag(nav_container, LV_OBJ_FLAG_SCROLLABLE);

    // Previous button
    lv_obj_t *btn_prev = lv_btn_create(nav_container);
    lv_obj_set_size(btn_prev, 30, 30);
    lv_obj_set_pos(btn_prev, 0, 5);
    lv_obj_set_style_bg_color(btn_prev, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_prev, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_border_width(btn_prev, 1, 0);
    lv_obj_set_style_radius(btn_prev, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label_prev = lv_label_create(btn_prev);
    lv_label_set_text(label_prev, LV_SYMBOL_LEFT);
    lv_obj_center(label_prev);
    lv_obj_set_style_text_color(label_prev, COLOR_TEXT_SECONDARY, 0);

    // Home button (center)
    lv_obj_t *btn_home = lv_btn_create(nav_container);
    lv_obj_set_size(btn_home, 40, 40);
    lv_obj_set_pos(btn_home, 50, 0);
    lv_obj_set_style_bg_color(btn_home, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(btn_home, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_home, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(btn_home, 2, 0);
    lv_obj_set_style_radius(btn_home, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label_home = lv_label_create(btn_home);
    lv_label_set_text(label_home, LV_SYMBOL_HOME);
    lv_obj_center(label_home);
    lv_obj_set_style_text_color(label_home, COLOR_ACCENT_GREEN, 0);

    // Next button
    lv_obj_t *btn_next = lv_btn_create(nav_container);
    lv_obj_set_size(btn_next, 30, 30);
    lv_obj_set_pos(btn_next, 110, 5);
    lv_obj_set_style_bg_color(btn_next, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_next, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_radius(btn_next, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label_next = lv_label_create(btn_next);
    lv_label_set_text(label_next, LV_SYMBOL_RIGHT);
    lv_obj_center(label_next);
    lv_obj_set_style_text_color(label_next, COLOR_TEXT_SECONDARY, 0);

    // Add pulse animation to ring
    lv_anim_init(&anim_pulse);
    lv_anim_set_var(&anim_pulse, ring_outer);
    lv_anim_set_values(&anim_pulse, 60, 100);
    lv_anim_set_time(&anim_pulse, ANIM_RING_PULSE_PERIOD);
    lv_anim_set_playback_time(&anim_pulse, ANIM_RING_PULSE_PERIOD);
    lv_anim_set_repeat_count(&anim_pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim_pulse, anim_pulse_callback);
    lv_anim_start(&anim_pulse);

    ESP_LOGI(TAG, "Home screen created with listening ring");

    return root;
}

void ui_animate_ring_pulse(lv_obj_t *ring) {
    // Pulse: opacity 60% to 100% over 2 seconds, repeat
    lv_anim_init(&anim_pulse);
    lv_anim_set_var(&anim_pulse, ring);
    lv_anim_set_values(&anim_pulse, 60, 100);
    lv_anim_set_time(&anim_pulse, ANIM_RING_PULSE_PERIOD);
    lv_anim_set_playback_time(&anim_pulse, ANIM_RING_PULSE_PERIOD);
    lv_anim_set_repeat_count(&anim_pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim_pulse, anim_pulse_callback);
    lv_anim_start(&anim_pulse);
}

void ui_animate_ring_spin(lv_obj_t *ring) {
    // Spin: rotate 0° to 360° over 1.5 seconds, repeat
    lv_anim_init(&anim_spin);
    lv_anim_set_var(&anim_spin, ring);
    lv_anim_set_values(&anim_spin, 0, 360);
    lv_anim_set_time(&anim_spin, ANIM_RING_SPIN_PERIOD);
    lv_anim_set_repeat_count(&anim_spin, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim_spin, anim_spin_callback);
    lv_anim_start(&anim_spin);
}

void ui_stop_ring_animation(void) {
    lv_anim_del(NULL, NULL);  // Delete all animations on NULL variable
}

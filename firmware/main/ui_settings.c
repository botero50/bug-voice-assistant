#include "ui_settings.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_SETTINGS";

lv_obj_t *ui_create_settings_screen(void) {
    ESP_LOGI(TAG, "Creating settings screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 380);
    lv_obj_set_pos(container, 33, 40);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header with back button
    lv_obj_t *header = lv_obj_create(container);
    lv_obj_set_size(header, 390, 30);
    lv_obj_set_pos(header, 5, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_0, 0);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 30, 30);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(back_btn, LV_OPA_0, 0);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "↶");
    lv_obj_center(back_label);
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_set_pos(title, 40, 5);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Settings list
    lv_obj_t *list = lv_obj_create(container);
    lv_obj_set_size(list, 390, 330);
    lv_obj_set_pos(list, 5, 40);
    lv_obj_set_style_bg_opa(list, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y_pos = 0;

    // Brightness setting
    lv_obj_t *bright_label = lv_label_create(list);
    lv_label_set_text(bright_label, "☀ Brightness");
    lv_obj_set_pos(bright_label, 10, y_pos);
    lv_obj_set_style_text_color(bright_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bright_label, &lv_font_montserrat_14, 0);

    lv_obj_t *bright_slider = lv_slider_create(list);
    lv_obj_set_size(bright_slider, 300, 20);
    lv_obj_set_pos(bright_slider, 10, y_pos + 20);
    lv_slider_set_range(bright_slider, 10, 100);
    lv_slider_set_value(bright_slider, 70, LV_ANIM_OFF);

    y_pos += 50;

    // Volume setting
    lv_obj_t *vol_label = lv_label_create(list);
    lv_label_set_text(vol_label, "🔊 Volume");
    lv_obj_set_pos(vol_label, 10, y_pos);
    lv_obj_set_style_text_color(vol_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(vol_label, &lv_font_montserrat_14, 0);

    lv_obj_t *vol_slider = lv_slider_create(list);
    lv_obj_set_size(vol_slider, 300, 20);
    lv_obj_set_pos(vol_slider, 10, y_pos + 20);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, 80, LV_ANIM_OFF);

    y_pos += 50;

    // Microphone toggle
    lv_obj_t *mic_label = lv_label_create(list);
    lv_label_set_text(mic_label, "🎤 Microphone");
    lv_obj_set_pos(mic_label, 10, y_pos);
    lv_obj_set_style_text_color(mic_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(mic_label, &lv_font_montserrat_14, 0);

    lv_obj_t *mic_toggle = lv_btn_create(list);
    lv_obj_set_size(mic_toggle, 60, 30);
    lv_obj_set_pos(mic_toggle, 300, y_pos);
    lv_obj_set_style_bg_color(mic_toggle, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_bg_opa(mic_toggle, LV_OPA_30, 0);
    lv_obj_set_style_border_opa(mic_toggle, LV_OPA_0, 0);
    lv_obj_set_style_radius(mic_toggle, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *toggle_text = lv_label_create(mic_toggle);
    lv_label_set_text(toggle_text, "◉");
    lv_obj_center(toggle_text);
    lv_obj_set_style_text_color(toggle_text, COLOR_ACCENT_GREEN, 0);

    y_pos += 45;

    // Power profile
    lv_obj_t *power_label = lv_label_create(list);
    lv_label_set_text(power_label, "⚡ Power Profile");
    lv_obj_set_pos(power_label, 10, y_pos);
    lv_obj_set_style_text_color(power_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_14, 0);

    lv_obj_t *power_value = lv_label_create(list);
    lv_label_set_text(power_value, "Always-Listening");
    lv_obj_set_pos(power_value, 10, y_pos + 20);
    lv_obj_set_style_text_color(power_value, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(power_value, &lv_font_montserrat_12, 0);

    y_pos += 50;

    // WiFi toggle
    lv_obj_t *wifi_label = lv_label_create(list);
    lv_label_set_text(wifi_label, "🌐 WiFi");
    lv_obj_set_pos(wifi_label, 10, y_pos);
    lv_obj_set_style_text_color(wifi_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, 0);

    lv_obj_t *wifi_toggle = lv_btn_create(list);
    lv_obj_set_size(wifi_toggle, 60, 30);
    lv_obj_set_pos(wifi_toggle, 300, y_pos);
    lv_obj_set_style_bg_color(wifi_toggle, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_bg_opa(wifi_toggle, LV_OPA_30, 0);
    lv_obj_set_style_border_opa(wifi_toggle, LV_OPA_0, 0);
    lv_obj_set_style_radius(wifi_toggle, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *wifi_text = lv_label_create(wifi_toggle);
    lv_label_set_text(wifi_text, "◉");
    lv_obj_center(wifi_text);
    lv_obj_set_style_text_color(wifi_text, COLOR_ACCENT_GREEN, 0);

    y_pos += 45;

    // Battery indicator
    lv_obj_t *battery_label = lv_label_create(list);
    lv_label_set_text(battery_label, "🔋 Battery: 73%");
    lv_obj_set_pos(battery_label, 10, y_pos);
    lv_obj_set_style_text_color(battery_label, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_14, 0);

    ESP_LOGI(TAG, "Settings screen created");

    return root;
}

lv_obj_t *ui_create_power_profile_selector(void) {
    ESP_LOGI(TAG, "Creating power profile selector");

    lv_obj_t *root = lv_scr_act();

    // Main container
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 380, 350);
    lv_obj_set_pos(container, 43, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Title
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Power Profile");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Circular menu with three options
    const char *profiles[] = {"Always\nListening", "Periodic\nWake", "Gesture\nOnly"};
    const int angles[] = {270, 30, 150};  // 12 o'clock, 1 o'clock, 5 o'clock

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_size(btn, 80, 80);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_bg_opa(btn, i == 0 ? LV_OPA_30 : LV_OPA_10, 0);  // First selected
        lv_obj_set_style_border_color(btn, COLOR_ACCENT_GREEN, 0);
        lv_obj_set_style_border_width(btn, i == 0 ? 2 : 1, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_50, 0);

        // Position in circle: radius 100px around center
        int x = 150 + (int)(100 * cosf((angles[i] - 90) * M_PI / 180.0)) - 40;
        int y = 150 + (int)(100 * sinf((angles[i] - 90) * M_PI / 180.0)) - 40;
        lv_obj_set_pos(btn, x, y);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, profiles[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, i == 0 ? COLOR_ACCENT_GREEN : COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

    // Center label
    lv_obj_t *center_label = lv_label_create(container);
    lv_label_set_text(center_label, "Power\nProfile");
    lv_obj_align(center_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(center_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(center_label, &lv_font_montserrat_14, 0);

    // Helper text
    lv_obj_t *helper = lv_label_create(container);
    lv_label_set_text(helper, "Swipe to select\nVoice: say profile");
    lv_obj_align(helper, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_color(helper, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(helper, &lv_font_montserrat_10, 0);

    ESP_LOGI(TAG, "Power profile selector created");

    return root;
}

#include "ui_core.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "bug_fonts.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_CORE";
static ui_context_t ui_ctx = {0};

const lv_font_t *font_large = NULL;
const lv_font_t *font_heading = NULL;
const lv_font_t *font_body = NULL;
const lv_font_t *font_small = NULL;

static lv_obj_t *time_label = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *battery_label = NULL;
static lv_obj_t *banner_label = NULL;

#define COLOR_BATTERY_YELLOW lv_color_make(255, 193, 7)

static lv_obj_t *transparent_box(lv_obj_t *parent, int width, int height) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, height);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

// Top: wifi and time. Bottom: battery. They live on LVGL's top layer, so they
// stay put while the screens underneath change. Both sit in the round screen's
// narrow ends, so they're small and centered.
static void ui_create_status_bar(void) {
    lv_obj_t *layer = lv_layer_top();

    ui_ctx.status_bar = transparent_box(layer, 220, 40);
    lv_obj_align(ui_ctx.status_bar, LV_ALIGN_TOP_MID, 0, 24);

    time_label = lv_label_create(ui_ctx.status_bar);
    lv_obj_set_style_text_font(time_label, font_large, 0);
    lv_obj_set_style_text_color(time_label, COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(time_label, "--:--");
    lv_obj_center(time_label);

    wifi_label = lv_label_create(ui_ctx.status_bar);
    lv_obj_set_style_text_font(wifi_label, font_small, 0);
    lv_obj_set_style_text_color(wifi_label, COLOR_ACCENT_GRAY, 0);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Shown instead of the clock while the user talks to Bug, on any screen
    banner_label = lv_label_create(ui_ctx.status_bar);
    lv_obj_set_style_text_font(banner_label, font_body, 0);
    lv_label_set_text(banner_label, "");
    lv_obj_add_flag(banner_label, LV_OBJ_FLAG_HIDDEN);

    ui_ctx.bottom_nav = transparent_box(layer, 160, 28);
    lv_obj_align(ui_ctx.bottom_nav, LV_ALIGN_BOTTOM_MID, 0, -26);

    battery_label = lv_label_create(ui_ctx.bottom_nav);
    lv_obj_set_style_text_font(battery_label, font_small, 0);
    lv_obj_set_style_text_color(battery_label, COLOR_TEXT_SECONDARY, 0);
    lv_label_set_text(battery_label, "--%");
    lv_obj_center(battery_label);
}

void ui_init(void) {
    ESP_LOGI(TAG, "Initializing UI (466x466 circular AMOLED)");

    ui_ctx.current_screen = SCREEN_HOME;
    ui_ctx.voice_state = VOICE_STATE_IDLE;
    ui_ctx.battery_percent = 100;
    ui_ctx.current_hour = 99;
    ui_ctx.current_minute = 0;
    ui_ctx.mic_muted = false;
    ui_ctx.wifi_connected = false;

    bug_fonts_init();
    font_large = bug_font_time;
    font_heading = bug_font_body;
    font_body = bug_font_body;
    font_small = bug_font_small;

    lv_disp_t *disp = lv_disp_get_default();
    lv_disp_set_theme(disp, lv_theme_default_init(disp, COLOR_ACCENT_GREEN, COLOR_ACCENT_BLUE, true, font_body));

    ui_ctx.root_screen = lv_scr_act();
    ui_create_status_bar();
    ui_nav_start();

    ESP_LOGI(TAG, "UI initialization complete");
}

void ui_deinit(void) {
    ESP_LOGI(TAG, "UI deinitialized");
}

void ui_set_screen(screen_state_t screen) {
    switch (screen) {
        case SCREEN_HOME:
            ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_NONE);
            break;
        case SCREEN_EMAIL_LIST:
            ui_nav_go(UI_SCREEN_EMAIL, LV_SCR_LOAD_ANIM_NONE);
            break;
        case SCREEN_CALENDAR:
            ui_nav_go(UI_SCREEN_CALENDAR, LV_SCR_LOAD_ANIM_NONE);
            break;
        case SCREEN_SETTINGS:
            ui_nav_go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_NONE);
            break;
        default:
            break;
    }
}

void ui_set_voice_state(voice_state_t state) {
    ui_ctx.voice_state = state;
}

void ui_update_power(int percent, bool charging) {
    if (!battery_label) {
        return;
    }
    if (percent < 0) {
        lv_label_set_text(battery_label, "--%");
        lv_obj_set_style_text_color(battery_label, COLOR_TEXT_SECONDARY, 0);
        return;
    }

    const char *symbol = percent > 75 ? LV_SYMBOL_BATTERY_FULL
                       : percent > 50 ? LV_SYMBOL_BATTERY_3
                       : percent > 25 ? LV_SYMBOL_BATTERY_2
                       : percent > 10 ? LV_SYMBOL_BATTERY_1
                                      : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(battery_label, "%s%s %d%%", charging ? LV_SYMBOL_CHARGE " " : "", symbol, percent);

    lv_color_t color = percent > 50 ? COLOR_ACCENT_GREEN : percent >= 25 ? COLOR_BATTERY_YELLOW : COLOR_ACCENT_RED;
    lv_obj_set_style_text_color(battery_label, charging ? COLOR_ACCENT_GREEN : color, 0);
    ui_ctx.battery_percent = percent;
}

void ui_update_battery(uint8_t percent) {
    ui_update_power(percent, false);
}

void ui_update_time(uint16_t hour, uint16_t minute) {
    if (!time_label) {
        return;
    }
    if (hour > 23) {
        if (ui_ctx.current_hour != 99) {
            ui_ctx.current_hour = 99;
            lv_label_set_text(time_label, "--:--");
        }
        return;
    }
    if (hour == ui_ctx.current_hour && minute == ui_ctx.current_minute) {
        return;
    }
    ui_ctx.current_hour = hour;
    ui_ctx.current_minute = minute;
    lv_label_set_text_fmt(time_label, "%02u:%02u", hour, minute);
}

void ui_update_wifi_status(bool connected) {
    if (!wifi_label || connected == ui_ctx.wifi_connected) {
        return;
    }
    ui_ctx.wifi_connected = connected;
    lv_obj_set_style_text_color(wifi_label, connected ? COLOR_ACCENT_GREEN : COLOR_ACCENT_GRAY, 0);
}

void ui_voice_banner(const char *text, lv_color_t color) {
    if (!banner_label || !time_label) {
        return;
    }
    if (text && text[0]) {
        lv_label_set_text(banner_label, text);
        lv_obj_set_style_text_color(banner_label, color, 0);
        lv_obj_center(banner_label);
        lv_obj_clear_flag(banner_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(time_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(banner_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(time_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_mute_mic(bool muted) {
    ui_ctx.mic_muted = muted;
}

void ui_show_voice_feedback(voice_state_t state, const char *text) {
    ui_set_voice_state(state);
}

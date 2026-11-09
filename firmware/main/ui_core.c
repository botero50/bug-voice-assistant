#include "ui_core.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "bug_fonts.h"
#include "dev_bridge.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

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
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_HIDDEN);  // the Home has its own big clock; this one stays hidden (kept only so the time is tracked)

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
    }
}

static lv_obj_t *notice_box = NULL;
static lv_timer_t *notice_timer = NULL;

static void notice_hide_cb(lv_timer_t *t) {
    if (notice_box) {
        lv_obj_add_flag(notice_box, LV_OBJ_FLAG_HIDDEN);
    }
    notice_timer = NULL;  // LVGL deletes a timer with repeat count 1 after this runs
}

void ui_notice(const char *text, lv_color_t color, uint32_t ms) {
    lv_obj_t *layer = lv_layer_top();
    if (!notice_box) {
        notice_box = lv_obj_create(layer);
        lv_obj_remove_style_all(notice_box);
        lv_obj_set_size(notice_box, 250, 40);
        lv_obj_set_style_radius(notice_box, 20, 0);
        lv_obj_set_style_bg_color(notice_box, lv_color_make(30, 30, 30), 0);
        lv_obj_set_style_bg_opa(notice_box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(notice_box, 2, 0);
        lv_obj_clear_flag(notice_box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(notice_box, LV_ALIGN_BOTTOM_MID, 0, -66);
        lv_obj_t *label = lv_label_create(notice_box);
        lv_obj_set_style_text_font(label, font_small, 0);
        lv_obj_center(label);
    }
    lv_obj_t *label = lv_obj_get_child(notice_box, 0);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_border_color(notice_box, color, 0);
    lv_obj_center(label);
    lv_obj_clear_flag(notice_box, LV_OBJ_FLAG_HIDDEN);
    if (notice_timer) {
        lv_timer_del(notice_timer);
    }
    notice_timer = lv_timer_create(notice_hide_cb, ms, NULL);
    lv_timer_set_repeat_count(notice_timer, 1);
}

// ------------------------------------------------------------------------------- permission sheet (Claude asks, you answer)
static lv_obj_t *approval_box = NULL;
static int approval_id = 0;

static void approval_hide(void) {
    if (approval_box) lv_obj_add_flag(approval_box, LV_OBJ_FLAG_HIDDEN);
}

static void approval_button_cb(lv_event_t *event) {
    bool allow = lv_event_get_user_data(event) != NULL;
    dev_bridge_decide(approval_id, allow);
    approval_hide();
    ui_notice(allow ? "Aprobado" : "Rechazado", allow ? COLOR_ACCENT_GREEN : COLOR_ACCENT_RED, 2500);
}

void ui_approval_show(const char *text, int id) {
    lv_obj_t *layer = lv_layer_top();
    approval_id = id;
    if (!approval_box) {
        approval_box = lv_obj_create(layer);
        lv_obj_remove_style_all(approval_box);
        lv_obj_set_size(approval_box, 340, 250);
        lv_obj_set_style_radius(approval_box, 28, 0);
        lv_obj_set_style_bg_color(approval_box, lv_color_make(24, 24, 28), 0);
        lv_obj_set_style_bg_opa(approval_box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(approval_box, 3, 0);
        lv_obj_set_style_border_color(approval_box, lv_color_make(255, 170, 0), 0);
        lv_obj_clear_flag(approval_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(approval_box);

        lv_obj_t *label = lv_label_create(approval_box);
        lv_obj_set_width(label, 290);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(label, font_body, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 22);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *button = lv_btn_create(approval_box);
            lv_obj_set_size(button, 136, 54);
            lv_obj_set_style_radius(button, 27, 0);
            lv_obj_set_style_bg_color(button, i == 0 ? lv_color_make(22, 130, 60) : lv_color_make(170, 40, 40), 0);
            lv_obj_align(button, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT, i == 0 ? 16 : -16, -16);
            lv_obj_add_event_cb(button, approval_button_cb, LV_EVENT_CLICKED, i == 0 ? (void *)1 : NULL);
            lv_obj_t *caption = lv_label_create(button);
            lv_obj_set_style_text_font(caption, font_body, 0);
            lv_label_set_text(caption, i == 0 ? "Aprobar" : "Rechazar");
            lv_obj_center(caption);
        }
    }
    lv_label_set_text(lv_obj_get_child(approval_box, 0), text);
    lv_obj_move_foreground(approval_box);
    lv_obj_clear_flag(approval_box, LV_OBJ_FLAG_HIDDEN);
}

// Called by the main loop: closes the sheet once the question was answered by voice or the PC stopped waiting.
void ui_approval_sync(int pending_id) {
    if (approval_box && pending_id == 0 && !lv_obj_has_flag(approval_box, LV_OBJ_FLAG_HIDDEN)) approval_hide();
}

// ------------------------------------------------------------------------------- choices and long answers (scrollable)
static lv_obj_t *choices_box = NULL;
static char choice_labels[4][120];

static void choices_close_cb(lv_event_t *event) {
    dev_bridge_choice_close();
    if (choices_box) lv_obj_add_flag(choices_box, LV_OBJ_FLAG_HIDDEN);
}

static void choice_pick_cb(lv_event_t *event) {
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    char order[160];
    snprintf(order, sizeof(order), "Elijo: %s", choice_labels[index]);
    dev_bridge_send("say", 0, false, order);  // also closes the list
    if (choices_box) lv_obj_add_flag(choices_box, LV_OBJ_FLAG_HIDDEN);
    ui_notice("Enviado a Claude", COLOR_ACCENT_GREEN, 2500);
}

static lv_obj_t *wrapped_label(lv_obj_t *parent, const char *text, lv_color_t color, int width) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font_small, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

// count >= 2: the question in full and one button per option (full text, wrapped). count 0: just a long text to read. Both scroll.
void ui_choices_show(const char *text, const char labels[][120], int count, lv_color_t accent) {
    if (count == 1 || count > 4) count = count > 4 ? 4 : 0;
    if (choices_box) lv_obj_del(choices_box);
    choices_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(choices_box);
    lv_obj_set_size(choices_box, 340, 340);
    lv_obj_set_style_radius(choices_box, 60, 0);
    lv_obj_set_style_bg_color(choices_box, lv_color_make(24, 24, 28), 0);
    lv_obj_set_style_bg_opa(choices_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(choices_box, 3, 0);
    lv_obj_set_style_border_color(choices_box, count ? lv_color_make(255, 170, 0) : accent, 0);
    lv_obj_set_style_pad_all(choices_box, 0, 0);
    lv_obj_set_style_pad_top(choices_box, 40, 0);
    lv_obj_set_style_pad_bottom(choices_box, 40, 0);
    lv_obj_set_style_pad_left(choices_box, 30, 0);
    lv_obj_set_style_pad_right(choices_box, 30, 0);
    lv_obj_set_style_pad_row(choices_box, 10, 0);
    lv_obj_set_flex_flow(choices_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(choices_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(choices_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(choices_box, LV_DIR_VER);
    lv_obj_center(choices_box);

    wrapped_label(choices_box, text, COLOR_TEXT_PRIMARY, 276);
    for (int i = 0; i < count; i++) {
        strlcpy(choice_labels[i], labels[i], sizeof(choice_labels[i]));
        lv_obj_t *button = lv_btn_create(choices_box);
        lv_obj_set_width(button, 276);
        lv_obj_set_height(button, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(button, 18, 0);
        lv_obj_set_style_pad_all(button, 10, 0);
        lv_obj_set_style_bg_color(button, lv_color_make(22, 100, 160), 0);
        lv_obj_add_event_cb(button, choice_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *caption = wrapped_label(button, labels[i], COLOR_TEXT_PRIMARY, 250);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *close = lv_btn_create(choices_box);
    lv_obj_set_size(close, 120, 34);
    lv_obj_set_style_radius(close, 17, 0);
    lv_obj_set_style_bg_color(close, lv_color_make(60, 60, 66), 0);
    lv_obj_add_event_cb(close, choices_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_obj_set_style_text_font(close_label, font_small, 0);
    lv_label_set_text(close_label, "Cerrar");
    lv_obj_center(close_label);
}

// ------------------------------------------------------------------------------- picture of the PC's screen
static lv_obj_t *shot_box = NULL;
static lv_img_dsc_t shot_dsc;
static lv_timer_t *shot_timer = NULL;

static void shot_close(void) {
    if (shot_timer) {
        lv_timer_del(shot_timer);
        shot_timer = NULL;
    }
    if (shot_box) {
        lv_obj_del(shot_box);
        shot_box = NULL;
    }
}

static void shot_close_cb(lv_event_t *event) {
    shot_close();
}

static void shot_timeout_cb(lv_timer_t *timer) {
    shot_timer = NULL;  // a timer with one repeat is deleted by LVGL after it runs
    shot_close();
}

void ui_shot_show(const uint8_t *pixels, int w, int h) {
    shot_close();
    memset(&shot_dsc, 0, sizeof(shot_dsc));
    shot_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    shot_dsc.header.w = w;
    shot_dsc.header.h = h;
    shot_dsc.data_size = (uint32_t)w * (uint32_t)h * 2;
    shot_dsc.data = pixels;

    shot_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(shot_box);
    lv_obj_set_size(shot_box, w + 16, h + 54);
    lv_obj_set_style_radius(shot_box, 24, 0);
    lv_obj_set_style_bg_color(shot_box, lv_color_make(14, 14, 18), 0);
    lv_obj_set_style_bg_opa(shot_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(shot_box, 2, 0);
    lv_obj_set_style_border_color(shot_box, lv_color_make(60, 150, 220), 0);
    lv_obj_clear_flag(shot_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(shot_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(shot_box, shot_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(shot_box);

    lv_obj_t *image = lv_img_create(shot_box);
    lv_img_set_src(image, &shot_dsc);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *caption = lv_label_create(shot_box);
    lv_obj_set_style_text_font(caption, font_small, 0);
    lv_obj_set_style_text_color(caption, COLOR_TEXT_SECONDARY, 0);
    lv_label_set_text(caption, "Tu pantalla · toca para cerrar");
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -8);

    shot_timer = lv_timer_create(shot_timeout_cb, 120000, NULL);  // closes by itself after two minutes: the buffer is reused by the next picture
    lv_timer_set_repeat_count(shot_timer, 1);
}

void ui_choices_sync(bool open) {
    if (choices_box && !open && !lv_obj_has_flag(choices_box, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(choices_box, LV_OBJ_FLAG_HIDDEN);
}

void ui_mute_mic(bool muted) {
    ui_ctx.mic_muted = muted;
}

void ui_show_voice_feedback(voice_state_t state, const char *text) {
    ui_set_voice_state(state);
}

#include "ui_stopwatch.h"
#include "bug_fonts.h"
#include "stopwatch.h"
#include "ui_config.h"
#include "ui_nav.h"

#define TICK_MS 100
#define COLOR_STOPWATCH lv_color_make(255, 160, 40)

static lv_obj_t *time_label = NULL;
static lv_obj_t *toggle_label = NULL;
static lv_timer_t *timer = NULL;

static void refresh(void) {
    if (!time_label) {
        return;
    }
    int64_t ms = stopwatch_elapsed_ms();
    int tenths = (int)(ms / 100 % 10);
    int seconds = (int)(ms / 1000 % 60);
    int minutes = (int)(ms / 60000 % 60);
    int hours = (int)(ms / 3600000);
    if (hours > 0) {
        lv_label_set_text_fmt(time_label, "%d:%02d:%02d", hours, minutes, seconds);
    } else {
        lv_label_set_text_fmt(time_label, "%02d:%02d.%d", minutes, seconds, tenths);
    }
    lv_label_set_text(toggle_label, stopwatch_running() ? LV_SYMBOL_PAUSE "  Pausa" : LV_SYMBOL_PLAY "  Iniciar");
}

static void tick_cb(lv_timer_t *t) {
    refresh();
}

static void toggle_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        stopwatch_toggle();
        refresh();
    }
}

static void reset_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        stopwatch_command("reset");
        refresh();
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    time_label = NULL;
    toggle_label = NULL;
}

static lv_obj_t *add_button(lv_obj_t *screen, int x_offset, int width, lv_color_t color, lv_event_cb_t cb, lv_obj_t **label_out) {
    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_set_size(button, width, 46);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x_offset, 288);
    lv_obj_set_style_radius(button, 23, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, bug_font_small, 0);
    lv_obj_center(label);
    *label_out = label;
    return button;
}

lv_obj_t *ui_stopwatch_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Cronómetro");
    lv_obj_set_style_text_font(title, bug_font_body, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    time_label = lv_label_create(screen);
    lv_obj_set_style_text_font(time_label, bug_font_huge, 0);
    lv_obj_set_style_text_color(time_label, COLOR_STOPWATCH, 0);
    lv_label_set_text(time_label, "00:00.0");
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_t *reset_label;
    add_button(screen, -78, 140, COLOR_ACCENT_BLUE, toggle_cb, &toggle_label);
    add_button(screen, 78, 140, lv_color_make(60, 60, 60), reset_cb, &reset_label);
    lv_label_set_text(reset_label, LV_SYMBOL_REFRESH "  Cero");

    refresh();
    timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    return screen;
}

#include "ui_alarm.h"
#include "alarms.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "ui_nav.h"
#include <string.h>

#define RING_SIZE 150
#define RING_TOP 96
#define INNER_SIZE 104
#define POLL_MS 300
#define MAX_SCREEN_MS 55000

static int waited_ms = 0;
static char label[64] = "Temporizador";
static lv_obj_t *ring = NULL;
static lv_timer_t *timer = NULL;

void ui_alarm_set_label(const char *text) {
    strlcpy(label, text[0] ? text : "Temporizador", sizeof(label));
}

static void ring_opa_cb(void *obj, int32_t value) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void stop_and_leave(void) {
    alarm_ring_stop();
    ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_FADE_IN);
}

static void stop_cb(lv_event_t *e) {
    stop_and_leave();
}

static void poll_cb(lv_timer_t *t) {
    waited_ms += POLL_MS;
    // Closes when the ringing ends by itself, or after a hard limit even if the ringer is stuck
    if (!alarm_ringing() || waited_ms > MAX_SCREEN_MS) {
        alarm_ring_stop();
        ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_FADE_IN);
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    ring = NULL;
}

lv_obj_t *ui_alarm_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, RING_SIZE, RING_SIZE);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, COLOR_ACCENT_RED, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, RING_TOP);
    lv_obj_set_ext_click_area(ring, 24);
    lv_obj_add_event_cb(ring, stop_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, ring);
    lv_anim_set_values(&pulse, 90, 255);
    lv_anim_set_time(&pulse, 350);
    lv_anim_set_playback_time(&pulse, 350);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&pulse, ring_opa_cb);
    lv_anim_start(&pulse);

    lv_obj_t *inner = lv_obj_create(screen);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, INNER_SIZE, INNER_SIZE);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(inner, ring, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *bell = lv_label_create(screen);
    lv_label_set_text(bell, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(bell, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bell, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align_to(bell, ring, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *what = lv_label_create(screen);
    lv_obj_set_size(what, 310, 56);
    lv_label_set_long_mode(what, LV_LABEL_LONG_DOT);
    lv_label_set_text(what, label);
    lv_obj_set_style_text_font(what, bug_font_body, 0);
    lv_obj_set_style_text_color(what, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(what, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(what, LV_ALIGN_TOP_MID, 0, 264);

    lv_obj_t *stop = lv_btn_create(screen);
    lv_obj_set_size(stop, 160, 38);
    lv_obj_align(stop, LV_ALIGN_TOP_MID, 0, 336);
    lv_obj_set_style_radius(stop, 19, 0);
    lv_obj_set_style_bg_color(stop, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_shadow_width(stop, 0, 0);
    lv_obj_add_event_cb(stop, stop_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP "  Detener");
    lv_obj_set_style_text_font(stop_label, bug_font_small, 0);
    lv_obj_center(stop_label);

    waited_ms = 0;
    timer = lv_timer_create(poll_cb, POLL_MS, NULL);
    return screen;
}

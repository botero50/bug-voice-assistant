#include "ui_music.h"
#include "assistant.h"
#include "bug_fonts.h"
#include "music_player.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_MUSIC";

#define RING_SIZE 150
#define RING_TOP 172
#define INNER_SIZE 104
#define BAR_COUNT 9
#define BAR_WIDTH 6
#define BAR_STEP 11
#define BAR_MIN_HEIGHT 6
#define BAR_MAX_HEIGHT 64
#define TICK_MS 90
#define NO_SIGNAL_AFTER_MS 20000  // music still not on after this long: the station isn't answering

#define COLOR_MUSIC lv_color_make(150, 90, 230)

static char source[24] = "Radio";
static char station[80] = "Radio";
static char detail[60] = "";
static lv_obj_t *source_label = NULL;
static lv_obj_t *station_label = NULL;
static lv_obj_t *detail_label = NULL;
static int seen_serial = 0;

static lv_obj_t *ring = NULL;
static lv_obj_t *hint = NULL;
static lv_obj_t *bars[BAR_COUNT];
static float levels[BAR_COUNT];
static lv_timer_t *timer = NULL;
static lv_obj_t *current_screen = NULL;  // only this screen's deletion may reset the globals
static uint32_t opened_ms = 0;

bool ui_music_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Music JSON could not be read");
        return false;
    }
    cJSON *name = cJSON_GetObjectItem(root, "station");
    cJSON *more = cJSON_GetObjectItem(root, "detail");
    cJSON *from = cJSON_GetObjectItem(root, "source");
    strlcpy(source, cJSON_IsString(from) && from->valuestring ? from->valuestring : "Radio", sizeof(source));
    strlcpy(station, cJSON_IsString(name) && name->valuestring ? name->valuestring : "Radio", sizeof(station));
    strlcpy(detail, cJSON_IsString(more) && more->valuestring ? more->valuestring : "", sizeof(detail));
    cJSON_Delete(root);
    music_set_label(station);
    return true;
}

static void apply_bars(void) {
    if (!ring) return;
    for (int i = 0; i < BAR_COUNT; i++) {
        int height = BAR_MIN_HEIGHT + (int)(levels[i] * (BAR_MAX_HEIGHT - BAR_MIN_HEIGHT));
        lv_obj_set_height(bars[i], height);
        lv_obj_align_to(bars[i], ring, LV_ALIGN_CENTER, (i - BAR_COUNT / 2) * BAR_STEP, 0);
    }
}

static void tick_cb(lv_timer_t *t) {
    if (!ring || !station_label) return;  // the screen is gone
    // A new song from the library started: show its name
    if (music_track_serial() != seen_serial) {
        seen_serial = music_track_serial();
        strlcpy(station, music_track_title(), sizeof(station));
        strlcpy(detail, music_track_artist(), sizeof(detail));
        lv_label_set_text(station_label, station);
        lv_label_set_text(detail_label, detail);
    }
    memmove(levels, levels + 1, (BAR_COUNT - 1) * sizeof(float));
    levels[BAR_COUNT - 1] = music_level();
    apply_bars();

    if (!music_wanted() && lv_tick_elaps(opened_ms) > NO_SIGNAL_AFTER_MS) {
        lv_label_set_text(hint, "La emisora no responde");
        lv_obj_set_style_text_color(hint, COLOR_ACCENT_RED, 0);
    }
}

static void ring_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        assistant_listen_now();
    }
}

static void stop_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        music_stop();
        ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP);
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    if (lv_event_get_target(e) != current_screen) return;  // an older screen going away after a newer one was built
    current_screen = NULL;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    ring = NULL;
    hint = NULL;
    for (int i = 0; i < BAR_COUNT; i++) bars[i] = NULL;
    source_label = station_label = detail_label = NULL;
}

static lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width, int height, int top,
                           const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
    return label;
}

lv_obj_t *ui_music_build(void) {
    if (timer) {  // an older music screen is still being replaced: its timer must not outlive it
        lv_timer_del(timer);
        timer = NULL;
    }
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);
    current_screen = screen;
    opened_ms = lv_tick_get();

    seen_serial = music_track_serial();
    source_label = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 280, 22, 66, source);
    station_label = add_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 320, 56, 88, station);
    detail_label = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 300, 22, 148, detail);

    ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, RING_SIZE, RING_SIZE);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, COLOR_MUSIC, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, RING_TOP);
    lv_obj_set_ext_click_area(ring, 20);
    lv_obj_add_event_cb(ring, ring_cb, LV_EVENT_SHORT_CLICKED, NULL);

    // On top of the ring, not inside it, and not clickable, so a tap anywhere on the ring reaches it
    lv_obj_t *inner = lv_obj_create(screen);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, INNER_SIZE, INNER_SIZE);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(inner, ring, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < BAR_COUNT; i++) {
        bars[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(bars[i]);
        lv_obj_set_size(bars[i], BAR_WIDTH, BAR_MIN_HEIGHT);
        lv_obj_set_style_radius(bars[i], BAR_WIDTH / 2, 0);
        lv_obj_set_style_bg_color(bars[i], COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(bars[i], LV_OBJ_FLAG_CLICKABLE);
        levels[i] = 0;
    }
    apply_bars();

    hint = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 300, 22, 328, "Toca el círculo para hablar");

    lv_obj_t *stop = lv_btn_create(screen);
    lv_obj_set_size(stop, 150, 34);
    lv_obj_align(stop, LV_ALIGN_TOP_MID, 0, 354);
    lv_obj_set_style_radius(stop, 17, 0);
    lv_obj_set_style_bg_color(stop, lv_color_make(42, 42, 42), 0);
    lv_obj_set_style_shadow_width(stop, 0, 0);
    lv_obj_add_event_cb(stop, stop_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP "  Detener");
    lv_obj_set_style_text_font(stop_label, bug_font_small, 0);
    lv_obj_center(stop_label);

    timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    return screen;
}

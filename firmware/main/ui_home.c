#include "ui_home.h"
#include "assistant.h"
#include "bug_fonts.h"
#include "settings_store.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "lvgl.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "UI_HOME";

// Layout for the 466 px round screen. The look follows the owner's "futuristic circular speaker" reference
// (style 1, cobot servo telemetry): a bezel of encoder ticks around the whole screen, a track ring with a bright
// arc that turns, 32 radial bars that are the voice meter, and a hub in the middle. The words are below.
// Only lines and arcs are drawn (no blur), so it stays light for the CPU and the AMOLED.
#define SCREEN_CENTER 233
#define CLUSTER_Y 140  // the hub, in the middle of the screen
#define TRACK_RADIUS 196  // the turning arc runs around the whole screen
#define METER_INNER 150  // the voice bars start here and grow outwards
#define METER_MAX 36
#define METER_BARS 32
#define TICKS 64
#define PI_F 3.14159265f
#define HUB_SIZE 70
#define HUB_INNER 46
#define SPINNER_SIZE 96
#define HEARD_TOP 214
#define HEARD_HEIGHT 24
#define ANSWER_TOP 244
#define ANSWER_HEIGHT 80
#define TEXT_WIDTH 250

#define COLOR_SETUP lv_color_make(245, 158, 11)
#define COLOR_SLATE lv_color_make(100, 116, 139)
#define COLOR_AMBER lv_color_make(34, 197, 94)  // listening is green
#define COLOR_CYAN lv_color_make(6, 182, 212)
#define COLOR_EMERALD lv_color_make(20, 184, 166)
#define COLOR_MUSIC lv_color_make(150, 90, 230)

static lv_obj_t *ring = NULL;  // the hub (touch target)
static lv_obj_t *mic_icon = NULL;
static lv_obj_t *spinner = NULL;
static lv_obj_t *heard_label = NULL;
static lv_obj_t *answer_label = NULL;
static lv_obj_t *db_label = NULL;
static lv_obj_t *track = NULL;  // track ring and its bright arc
static lv_obj_t *meter = NULL;  // draws the 32 voice bars
static lv_obj_t *bezel = NULL;  // draws the 64 ticks

static float smooth_level = 0;
static float phase = 0;
static float spin = 0;
static int idle_skips = 0;
static int shown_db = 1000;
static lv_color_t accent;
static float cos_tick[TICKS], sin_tick[TICKS], cos_bar[METER_BARS], sin_bar[METER_BARS];
static bool tables_ready = false;

// The latest state, applied again whenever the screen is built.
static ui_home_mode_t current_mode = UI_HOME_STANDBY;
static char current_heard[140] = "";
static char current_answer[260] = "";

static lv_obj_t *circle(lv_obj_t *parent, int size, lv_color_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ring_opa_cb(void *obj, int32_t value) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void ring_pulse(int low, int high, uint32_t half_period_ms) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ring);
    lv_anim_set_values(&anim, low, high);
    lv_anim_set_time(&anim, half_period_ms);
    lv_anim_set_playback_time(&anim, half_period_ms);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, ring_opa_cb);
    lv_anim_start(&anim);
}

static void prepare_tables(void) {
    for (int i = 0; i < TICKS; i++) {
        cos_tick[i] = cosf(2.0f * PI_F * i / TICKS);
        sin_tick[i] = sinf(2.0f * PI_F * i / TICKS);
    }
    for (int i = 0; i < METER_BARS; i++) {
        cos_bar[i] = cosf(2.0f * PI_F * i / METER_BARS);
        sin_bar[i] = sinf(2.0f * PI_F * i / METER_BARS);
    }
    tables_ready = true;
}

static void draw_ticks(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int cx = area.x1 + SCREEN_CENTER, cy = area.y1 + SCREEN_CENTER;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    for (int i = 0; i < TICKS; i++) {
        bool major = i % 8 == 0, mid = i % 4 == 0;
        int length = major ? 12 : (mid ? 7 : 4);
        dsc.color = major ? accent : (mid ? lv_color_make(95, 95, 95) : lv_color_make(40, 40, 40));
        dsc.width = major ? 3 : 2;
        lv_point_t from = {(lv_coord_t)(cx + (int)(cos_tick[i] * (226 - length))), (lv_coord_t)(cy + (int)(sin_tick[i] * (226 - length)))};
        lv_point_t to = {(lv_coord_t)(cx + (int)(cos_tick[i] * 226)), (lv_coord_t)(cy + (int)(sin_tick[i] * 226))};
        lv_draw_line(ctx, &dsc, &from, &to);
    }
}

static void draw_meter(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int cx = (area.x1 + area.x2) / 2, cy = (area.y1 + area.y2) / 2;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = 3;
    dsc.round_start = dsc.round_end = 1;
    for (int i = 0; i < METER_BARS; i++) {
        float shape = sinf(i * 0.6f + phase) * 0.5f + 0.5f;  // each bar follows the voice a little differently
        int length = 4 + (int)(shape * smooth_level * METER_MAX);
        dsc.color = i % 4 == 0 ? accent : lv_color_mix(accent, lv_color_black(), 140);
        lv_point_t from = {(lv_coord_t)(cx + (int)(cos_bar[i] * METER_INNER)), (lv_coord_t)(cy + (int)(sin_bar[i] * METER_INNER))};
        lv_point_t to = {(lv_coord_t)(cx + (int)(cos_bar[i] * (METER_INNER + length))), (lv_coord_t)(cy + (int)(sin_bar[i] * (METER_INNER + length)))};
        lv_draw_line(ctx, &dsc, &from, &to);
    }
}

static void apply_accent(lv_color_t color) {
    accent = color;
    if (track) {
        lv_obj_set_style_arc_color(track, color, LV_PART_INDICATOR);
        lv_obj_invalidate(track);
    }
    if (meter) lv_obj_invalidate(meter);
    if (bezel) lv_obj_invalidate(bezel);
    if (db_label) lv_obj_set_style_text_color(db_label, color, 0);
}

static void apply_mode(void) {
    lv_anim_del(ring, ring_opa_cb);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);

    bool hearing = current_mode == UI_HOME_HEARING || current_mode == UI_HOME_LISTENING;
    set_hidden(mic_icon, hearing);
    set_hidden(spinner, current_mode != UI_HOME_THINKING);
    lv_label_set_text(mic_icon, current_mode == UI_HOME_MUTED ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO);

    switch (current_mode) {
    case UI_HOME_STANDBY:
        lv_obj_set_style_bg_color(ring, COLOR_SLATE, 0);
        apply_accent(COLOR_SLATE);
        ring_pulse(110, 200, 2500);
        break;
    case UI_HOME_HEARING:
    case UI_HOME_LISTENING:
        lv_obj_set_style_bg_color(ring, COLOR_AMBER, 0);
        apply_accent(COLOR_AMBER);
        break;
    case UI_HOME_THINKING:
        lv_obj_set_style_bg_color(ring, COLOR_CYAN, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_50, 0);
        apply_accent(COLOR_CYAN);
        break;
    case UI_HOME_SPEAKING:
        lv_obj_set_style_bg_color(ring, COLOR_EMERALD, 0);
        apply_accent(COLOR_EMERALD);
        ring_pulse(150, 255, 250);
        break;
    case UI_HOME_MUTED:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_40, 0);
        apply_accent(COLOR_ACCENT_GRAY);
        break;
    case UI_HOME_MUSIC:
        lv_obj_set_style_bg_color(ring, COLOR_MUSIC, 0);
        apply_accent(COLOR_MUSIC);
        ring_pulse(130, 255, 900);
        break;
    case UI_HOME_SETUP:
        lv_obj_set_style_bg_color(ring, COLOR_SETUP, 0);
        apply_accent(COLOR_SETUP);
        ring_pulse(120, 255, 700);
        break;
    case UI_HOME_ERROR:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_RED, 0);
        apply_accent(COLOR_ACCENT_RED);
        break;
    }
}

void ui_home_set_mode(ui_home_mode_t mode) {
    current_mode = mode;
    ui_home_lumina_set_mode(mode);
    if (ring) {
        apply_mode();
    }
}

void ui_home_set_texts(const char *heard, const char *answer) {
    ui_home_lumina_set_texts(heard, answer);
    if (heard) {
        strlcpy(current_heard, heard, sizeof(current_heard));
        if (heard_label) lv_label_set_text(heard_label, current_heard);
    }
    if (answer) {
        strlcpy(current_answer, answer, sizeof(current_answer));
        if (answer_label) lv_label_set_text(answer_label, current_answer);
    }
}

// Called every 50 ms with the level of whatever is making sound: the microphone while listening, the speaker while
// answering, the music while it plays.
void ui_home_push_level(float level) {
    ui_home_lumina_push_level(level);
    if (!ring || !meter) {
        return;
    }
    level = level < 0 ? 0 : (level > 1 ? 1 : level);
    bool quiet = level < 0.04f && smooth_level < 0.04f;
    if (quiet && ++idle_skips < 10) {
        return;  // nothing is moving: look only twice a second
    }
    idle_skips = 0;
    smooth_level += (level - smooth_level) * 0.45f;
    phase += 0.35f + smooth_level * 0.5f;
    spin += quiet ? 1.0f : 2.0f + smooth_level * 9.0f;
    if (spin >= 360.0f) spin -= 360.0f;
    if (track) {
        int sweep = 90 + (int)(smooth_level * 140.0f);
        lv_arc_set_angles(track, (uint16_t)spin, (uint16_t)(((int)spin + sweep) % 360));
    }
    lv_obj_invalidate(meter);

    int db = -60 + (int)(smooth_level * 60.0f);
    if (db != shown_db && db_label) {
        shown_db = db;
        lv_label_set_text_fmt(db_label, "%d dB", db);
    }
}

void ui_home_ring_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SHORT_CLICKED && ui_nav_click_allowed()) {
        if (current_mode == UI_HOME_SETUP) {
            ui_nav_go(UI_SCREEN_WIFI, LV_SCR_LOAD_ANIM_MOVE_LEFT);
        } else {
            assistant_listen_now();
        }
    } else if (code == LV_EVENT_LONG_PRESSED && ui_nav_click_allowed()) {
        bug_settings_t settings;
        settings_copy(&settings);
        bool muted = !settings.mic_muted;
        settings_set_mic_muted(muted);
        assistant_set_muted(muted);
        ESP_LOGI(TAG, "Microphone %s", muted ? "muted" : "on");
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    ring = NULL;
    mic_icon = NULL;
    spinner = NULL;
    heard_label = NULL;
    answer_label = NULL;
    db_label = NULL;
    track = NULL;
    meter = NULL;
    bezel = NULL;
}

static lv_obj_t *text_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int top, int height) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_size(label, TEXT_WIDTH, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
    return label;
}

lv_obj_t *ui_home_build(void) {
    bug_settings_t chosen;
    settings_copy(&chosen);
    if (chosen.ui_style == 0) {  // the Lumina Kinetic look is the default
        return ui_home_lumina_build(current_mode, current_heard, current_answer);
    }
    if (!tables_ready) prepare_tables();
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);
    accent = COLOR_SLATE;
    smooth_level = 0;
    shown_db = 1000;

    // The bezel of ticks, behind everything
    bezel = lv_obj_create(screen);
    lv_obj_remove_style_all(bezel);
    lv_obj_set_size(bezel, 466, 466);
    lv_obj_align(bezel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bezel, draw_ticks, LV_EVENT_DRAW_MAIN_END, NULL);

    // The track ring with the turning bright arc
    track = lv_arc_create(screen);
    lv_obj_set_size(track, TRACK_RADIUS * 2, TRACK_RADIUS * 2);
    lv_obj_align(track, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(track, 0, 360);
    lv_arc_set_angles(track, 0, 120);
    lv_obj_remove_style(track, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(track, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(track, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_arc_width(track, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(track, false, LV_PART_INDICATOR);

    // The voice meter: 32 bars around the hub
    meter = lv_obj_create(screen);
    lv_obj_remove_style_all(meter);
    lv_obj_set_size(meter, TRACK_RADIUS * 2, TRACK_RADIUS * 2);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(meter, draw_meter, LV_EVENT_DRAW_MAIN_END, NULL);

    // The hub: a tap listens, a long press mutes the microphone
    ring = circle(screen, HUB_SIZE, COLOR_SLATE);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, CLUSTER_Y - HUB_SIZE / 2);
    lv_obj_set_ext_click_area(ring, 40);
    lv_obj_add_event_cb(ring, ui_home_ring_event, LV_EVENT_ALL, NULL);

    // These sit on top of the hub, so the hub can pulse alone. None of them take touches.
    lv_obj_t *inner = circle(screen, HUB_INNER, COLOR_BG_DARK);
    lv_obj_align_to(inner, ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);

    mic_icon = lv_label_create(screen);
    lv_obj_set_style_text_font(mic_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(mic_icon, COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(mic_icon, LV_SYMBOL_AUDIO);
    lv_obj_align_to(mic_icon, ring, LV_ALIGN_CENTER, 0, 0);

    spinner = lv_spinner_create(screen, 1000, 90);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_align_to(spinner, ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

    // The level in dB, under the cluster
    db_label = lv_label_create(screen);
    lv_obj_set_style_text_font(db_label, bug_font_small, 0);
    lv_obj_set_style_text_color(db_label, COLOR_SLATE, 0);
    lv_label_set_text(db_label, "-60 dB");
    lv_obj_align(db_label, LV_ALIGN_TOP_MID, 0, CLUSTER_Y + HUB_SIZE / 2 + 8);

    heard_label = text_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, HEARD_TOP, HEARD_HEIGHT);
    answer_label = text_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, ANSWER_TOP, ANSWER_HEIGHT);
    lv_label_set_text(heard_label, current_heard);
    lv_label_set_text(answer_label, current_answer);

    apply_mode();
    return screen;
}

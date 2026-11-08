#include "audio_driver.h"
#include "bug_fonts.h"
#include "settings_store.h"
#include "ui_config.h"
#include "ui_home.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// The "Lumina Kinetic" look of the Home screen (the owner's reference): a round, tactile speaker. A rim that is the
// volume dial, a bezel of ticks, 16 elastic petals that move with the voice (the microphone while listening, the
// speaker while answering, the music while it plays), two ripples when sound is on, and in the middle the clock or,
// while talking, the words. Only lines and arcs are drawn (no blur), so it stays light for the CPU and the AMOLED.
// Three palettes: ceramic and ochre (default), nordic forest, and mono with a red dot.

#define PI_F 3.14159265f
#define CENTER 233
#define PETALS 16
#define TICKS 48
#define PETAL_INNER 146
#define PETAL_LENGTH 14
#define PETAL_STRETCH 24
#define TICK_OUTER 214
#define RIM_SIZE 458
#define TOUCH_SIZE 300
#define TEXT_WIDTH 236

typedef struct {
    lv_color_t bg, core, accent, dot;
} palette_t;

static palette_t palettes[3];
static bool palettes_ready = false;

static lv_obj_t *backdrop = NULL;
static lv_obj_t *rim = NULL;
static lv_obj_t *petals_obj = NULL;
static lv_obj_t *clock_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *heard_label = NULL;
static lv_obj_t *answer_label = NULL;
static lv_obj_t *touch = NULL;

static const palette_t *palette = NULL;
static ui_home_mode_t mode = UI_HOME_STANDBY;
static float level_smooth = 0, phase = 0, spin = 0;
static float stretch[PETALS];
static int idle_skips = 0;
static int shown_minute = -1;
static int shown_volume = -1;
static float cos_t[PETALS], sin_t[PETALS];
static float cos_k[TICKS], sin_k[TICKS];
static bool tables_ready = false;

static void make_palettes(void) {
    if (palettes_ready) return;
    palettes[0] = (palette_t){lv_color_make(12, 12, 14), lv_color_make(234, 122, 84), lv_color_make(249, 160, 122), lv_color_make(255, 179, 150)};
    palettes[1] = (palette_t){lv_color_make(8, 13, 11), lv_color_make(16, 185, 129), lv_color_make(110, 231, 183), lv_color_make(167, 243, 208)};
    palettes[2] = (palette_t){lv_color_make(7, 7, 8), lv_color_make(255, 255, 255), lv_color_make(212, 212, 216), lv_color_make(255, 59, 48)};
    for (int i = 0; i < TICKS; i++) {
        cos_k[i] = cosf(2.0f * PI_F * i / TICKS);
        sin_k[i] = sinf(2.0f * PI_F * i / TICKS);
    }
    for (int i = 0; i < PETALS; i++) {
        cos_t[i] = cosf(2.0f * PI_F * i / PETALS);
        sin_t[i] = sinf(2.0f * PI_F * i / PETALS);
    }
    palettes_ready = true;
    tables_ready = true;
}

static bool active_state(void) {
    return mode == UI_HOME_HEARING || mode == UI_HOME_LISTENING || mode == UI_HOME_THINKING || mode == UI_HOME_SPEAKING;
}

// The colours of the petals follow the palette; some states tint them
static lv_color_t petal_core(void) {
    switch (mode) {
    case UI_HOME_MUTED:
        return lv_color_mix(palette->core, palette->bg, 70);
    case UI_HOME_ERROR:
        return COLOR_ACCENT_RED;
    case UI_HOME_SETUP:
        return lv_color_make(245, 158, 11);
    case UI_HOME_STANDBY:
        return lv_color_white();  // at rest the petals are white
    case UI_HOME_HEARING:
    case UI_HOME_LISTENING:
        return lv_color_make(34, 197, 94);  // listening is green
    default:
        return palette->core;
    }
}

static lv_color_t petal_accent(void) {
    switch (mode) {
    case UI_HOME_STANDBY:
        return lv_color_make(190, 190, 196);
    case UI_HOME_HEARING:
    case UI_HOME_LISTENING:
        return lv_color_make(134, 239, 172);
    default:
        return palette->accent;
    }
}

static void draw_backdrop(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int cx = area.x1 + CENTER, cy = area.y1 + CENTER;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    for (int i = 0; i < TICKS; i++) {
        bool major = i % 4 == 0;
        int length = major ? 9 : 5;
        dsc.color = lv_color_mix(lv_color_white(), palette->bg, major ? 55 : 18);
        dsc.width = major ? 2 : 1;
        lv_point_t a = {(lv_coord_t)(cx + (int)(cos_k[i] * (TICK_OUTER - length))), (lv_coord_t)(cy + (int)(sin_k[i] * (TICK_OUTER - length)))};
        lv_point_t b = {(lv_coord_t)(cx + (int)(cos_k[i] * TICK_OUTER)), (lv_coord_t)(cy + (int)(sin_k[i] * TICK_OUTER))};
        lv_draw_line(ctx, &dsc, &a, &b);
    }
    // The reference ring the petals orbit
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_mix(petal_core(), palette->bg, 38);
    arc.width = 2;
    lv_point_t center = {(lv_coord_t)cx, (lv_coord_t)cy};
    lv_draw_arc(ctx, &arc, &center, 205 - 9, 0, 360);
}

static void draw_petals(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int cx = (area.x1 + area.x2) / 2, cy = (area.y1 + area.y2) / 2;
    bool sound = mode == UI_HOME_HEARING || mode == UI_HOME_LISTENING || mode == UI_HOME_SPEAKING || mode == UI_HOME_MUSIC;

    // Two ripples that spread while there is sound
    if (sound) {
        lv_draw_arc_dsc_t ripple;
        lv_draw_arc_dsc_init(&ripple);
        ripple.width = 2;
        lv_point_t center = {(lv_coord_t)cx, (lv_coord_t)cy};
        for (int w = 0; w < 2; w++) {
            float travel = fmodf(phase * 6.0f + w * 38.0f, 76.0f);
            int radius = 70 + (int)travel + (int)(level_smooth * 14.0f);
            int alpha = 150 - (int)(travel * 150.0f / 76.0f);
            ripple.color = petal_core();
            ripple.opa = (lv_opa_t)(alpha < 0 ? 0 : alpha > 110 ? 110 : alpha);
            lv_draw_arc(ctx, &ripple, &center, (uint16_t)radius, 0, 360);
        }
    }

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 6;
    line.round_start = line.round_end = 1;
    float base = PETAL_INNER + level_smooth * 10.0f;
    float rot = mode == UI_HOME_THINKING ? spin : spin * 0.25f;
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < PETALS; i++) {
        float c = cos_t[i] * cr - sin_t[i] * sr, s = sin_t[i] * cr + cos_t[i] * sr;  // petal angle plus the rotation
        float inner = base - (mode == UI_HOME_THINKING ? sinf(phase * 0.6f + i) * 8.0f : 0.0f);
        float outer = inner + PETAL_LENGTH + stretch[i];
        line.color = i % 2 == 0 ? petal_core() : lv_color_mix(petal_accent(), palette->bg, mode == UI_HOME_MUTED ? 90 : 255);
        lv_point_t a = {(lv_coord_t)(cx + (int)(c * inner)), (lv_coord_t)(cy + (int)(s * inner))};
        lv_point_t b = {(lv_coord_t)(cx + (int)(c * outer)), (lv_coord_t)(cy + (int)(s * outer))};
        lv_draw_line(ctx, &line, &a, &b);
        if (i == 0) {  // the Bauhaus dot on the first petal
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.bg_color = palette->dot;
            dot.radius = LV_RADIUS_CIRCLE;
            lv_area_t where = {(lv_coord_t)(b.x - 4), (lv_coord_t)(b.y - 4), (lv_coord_t)(b.x + 4), (lv_coord_t)(b.y + 4)};
            lv_draw_rect(ctx, &dot, &where);
        }
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// While talking the words take the middle; otherwise the clock does and the prompt sits under it
static void layout(void) {
    if (!backdrop) return;
    bool talking = active_state();
    set_hidden(clock_label, talking);
    set_hidden(date_label, talking);
    if (talking) {
        lv_obj_set_height(heard_label, 24);
        lv_obj_align(heard_label, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_set_height(answer_label, 84);
        lv_obj_align(answer_label, LV_ALIGN_TOP_MID, 0, 180);
    } else {
        lv_obj_set_height(heard_label, 24);
        lv_obj_align(heard_label, LV_ALIGN_TOP_MID, 0, 288);
        lv_obj_set_height(answer_label, 48);
        lv_obj_align(answer_label, LV_ALIGN_TOP_MID, 0, 236);
    }
}

static void update_clock(bool force) {
    if (!clock_label) return;
    time_t now = time(NULL);
    if (now < 1700000000) {
        if (force) {
            lv_label_set_text(clock_label, "--:--");
            lv_label_set_text(date_label, "");
        }
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    int stamp = t.tm_hour * 60 + t.tm_min;
    if (!force && stamp == shown_minute) return;
    shown_minute = stamp;
    static const char *const DAYS[] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
    static const char *const MONTHS[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
    lv_label_set_text_fmt(clock_label, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(date_label, "%s · %02d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
}

static void volume_cb(lv_event_t *e) {
    lv_obj_t *arc = lv_event_get_target(e);
    int value = lv_arc_get_value(arc);
    if (value < 5) value = 5;
    shown_volume = value;
    settings_set_volume((uint8_t)value);
    audio_set_volume(value);
}

bool ui_home_lumina_built(void) {
    return backdrop != NULL;
}

void ui_home_lumina_set_mode(ui_home_mode_t new_mode) {
    mode = new_mode;
    if (!backdrop) return;
    layout();
    lv_obj_invalidate(backdrop);
    lv_obj_invalidate(petals_obj);
}

void ui_home_lumina_set_texts(const char *heard, const char *answer) {
    if (!backdrop) return;
    if (heard) lv_label_set_text(heard_label, heard);
    if (answer) lv_label_set_text(answer_label, answer);
}

void ui_home_lumina_push_level(float level) {
    if (!backdrop) return;
    level = level < 0 ? 0 : (level > 1 ? 1 : level);
    bool sound = mode == UI_HOME_HEARING || mode == UI_HOME_LISTENING || mode == UI_HOME_SPEAKING || mode == UI_HOME_MUSIC;
    bool quiet = !sound && mode != UI_HOME_THINKING && level < 0.04f && level_smooth < 0.04f;
    if (quiet && ++idle_skips < 20) return;  // at rest it breathes very slowly: look about once a second
    idle_skips = 0;

    update_clock(false);
    int volume = lv_arc_get_value(rim);
    bug_settings_t settings;
    settings_copy(&settings);
    if (settings.volume != volume && !lv_obj_has_state(rim, LV_STATE_PRESSED)) lv_arc_set_value(rim, settings.volume);

    float target = level;
    if (mode == UI_HOME_THINKING) target = 0.3f;
    else if (quiet) target = 0.1f + sinf(phase * 0.5f) * 0.05f;
    level_smooth += (target - level_smooth) * 0.4f;
    phase += 0.2f + level_smooth * 0.3f;
    spin += mode == UI_HOME_THINKING ? 0.06f : 0.012f + level_smooth * 0.02f;
    for (int i = 0; i < PETALS; i++) {
        float want = 0;
        if (sound) {
            float wave = sinf(phase * 2.0f + i * 0.6f) * PETAL_STRETCH * (0.25f + level_smooth * 1.1f);
            want = wave > 0 ? wave : 0;
        }
        stretch[i] += (want - stretch[i]) * 0.3f;
    }
    lv_obj_invalidate(petals_obj);
}

static void deleted_cb(lv_event_t *e) {
    if (lv_event_get_target(e) != backdrop) return;
    backdrop = rim = petals_obj = clock_label = date_label = heard_label = answer_label = touch = NULL;
}

lv_obj_t *ui_home_lumina_build(ui_home_mode_t start_mode, const char *heard, const char *answer) {
    make_palettes();
    bug_settings_t settings;
    settings_copy(&settings);
    palette = &palettes[settings.palette > 2 ? 0 : settings.palette];
    mode = start_mode;
    level_smooth = 0;
    shown_minute = -1;
    idle_skips = 0;
    memset(stretch, 0, sizeof(stretch));

    lv_obj_t *screen = lv_obj_create(NULL);

    // The face: a round plate with the tick bezel
    backdrop = lv_obj_create(screen);
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, 466, 466);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, palette->bg, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(backdrop, draw_backdrop, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(backdrop, deleted_cb, LV_EVENT_DELETE, NULL);

    // The rim is the volume dial: drag along it
    rim = lv_arc_create(screen);
    lv_obj_set_size(rim, RIM_SIZE, RIM_SIZE);
    lv_obj_align(rim, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(rim, 270);
    lv_arc_set_bg_angles(rim, 0, 360);
    lv_arc_set_range(rim, 0, 100);
    lv_arc_set_value(rim, settings.volume);
    lv_obj_remove_style(rim, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(rim, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rim, lv_color_mix(lv_color_white(), palette->bg, 25), LV_PART_MAIN);
    lv_obj_set_style_arc_width(rim, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(rim, lv_color_mix(lv_color_white(), palette->bg, 115), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(rim, true, LV_PART_INDICATOR);
    lv_obj_add_event_cb(rim, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    petals_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(petals_obj);
    lv_obj_set_size(petals_obj, 430, 430);
    lv_obj_align(petals_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(petals_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(petals_obj, draw_petals, LV_EVENT_DRAW_MAIN_END, NULL);

    // The middle of the face is the touch target: a tap listens, a long press mutes the microphone
    touch = lv_obj_create(screen);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, TOUCH_SIZE, TOUCH_SIZE);
    lv_obj_align(touch, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(touch, ui_home_ring_event, LV_EVENT_ALL, NULL);

    clock_label = make_label(screen, bug_font_huge, COLOR_TEXT_PRIMARY, 220);
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 142);
    date_label = make_label(screen, bug_font_small, lv_color_mix(lv_color_white(), palette->bg, 120), 220);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 216);
    heard_label = make_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, TEXT_WIDTH);
    answer_label = make_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, TEXT_WIDTH);
    lv_label_set_text(heard_label, heard ? heard : "");
    lv_label_set_text(answer_label, answer ? answer : "");
    update_clock(true);
    layout();
    return screen;
}

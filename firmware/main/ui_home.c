#include "ui_home.h"
#include "ui_config.h"
#include "bug_fonts.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_HOME";

// Layout for the 466 px round screen. Everything stays inside the circle:
// below y=250 the usable width shrinks from 460 px to about 320 px at y=390.
#define RING_SIZE 150
#define RING_TOP 90
#define INNER_SIZE 104
#define SPINNER_SIZE 180
#define HEARD_TOP 254
#define HEARD_HEIGHT 42
#define ANSWER_TOP 298
#define ANSWER_HEIGHT 80
#define TEXT_WIDTH 310

#define BAR_COUNT 9
#define BAR_WIDTH 6
#define BAR_STEP 11
#define BAR_MIN_HEIGHT 6
#define BAR_MAX_HEIGHT 64

static lv_obj_t *ring = NULL;
static lv_obj_t *mic_icon = NULL;
static lv_obj_t *spinner = NULL;
static lv_obj_t *heard_label = NULL;
static lv_obj_t *answer_label = NULL;
static lv_obj_t *bars[BAR_COUNT];
static float levels[BAR_COUNT];

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

static void apply_bars(void) {
    for (int i = 0; i < BAR_COUNT; i++) {
        int height = BAR_MIN_HEIGHT + (int)(levels[i] * (BAR_MAX_HEIGHT - BAR_MIN_HEIGHT));
        lv_obj_set_height(bars[i], height);
        lv_obj_align_to(bars[i], ring, LV_ALIGN_CENTER, (i - BAR_COUNT / 2) * BAR_STEP, 0);
    }
}

void ui_home_set_mode(ui_home_mode_t mode) {
    if (!ring) {
        return;
    }

    lv_anim_del(ring, ring_opa_cb);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);

    bool show_bars = mode == UI_HOME_HEARING || mode == UI_HOME_LISTENING;
    set_hidden(mic_icon, show_bars);
    set_hidden(spinner, mode != UI_HOME_THINKING);
    for (int i = 0; i < BAR_COUNT; i++) {
        set_hidden(bars[i], !show_bars);
        levels[i] = 0;
    }
    if (show_bars) {
        apply_bars();
    }

    switch (mode) {
    case UI_HOME_STANDBY:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_GRAY, 0);
        ring_pulse(110, 200, 2500);
        break;
    case UI_HOME_HEARING:
    case UI_HOME_LISTENING:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_GREEN, 0);
        break;
    case UI_HOME_THINKING:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_BLUE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_50, 0);
        break;
    case UI_HOME_SPEAKING:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_WHITE, 0);
        ring_pulse(150, 255, 250);  // 2 Hz
        break;
    case UI_HOME_ERROR:
        lv_obj_set_style_bg_color(ring, COLOR_ACCENT_RED, 0);
        break;
    }
}

void ui_home_set_texts(const char *heard, const char *answer) {
    if (heard_label && heard) {
        lv_label_set_text(heard_label, heard);
    }
    if (answer_label && answer) {
        lv_label_set_text(answer_label, answer);
    }
}

void ui_home_push_level(float level) {
    if (!ring || lv_obj_has_flag(bars[0], LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    memmove(levels, levels + 1, (BAR_COUNT - 1) * sizeof(float));
    levels[BAR_COUNT - 1] = level < 0 ? 0 : (level > 1 ? 1 : level);
    apply_bars();
}

static lv_obj_t *text_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int top, int height) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_size(label, TEXT_WIDTH, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
    lv_label_set_text(label, "");
    return label;
}

lv_obj_t *ui_create_home_screen(void) {
    ESP_LOGI(TAG, "Creating home screen");
    lv_obj_t *root = lv_scr_act();

    ring = circle(root, RING_SIZE, COLOR_ACCENT_GRAY);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, RING_TOP);

    // These sit on top of the ring instead of inside it, so the ring can pulse alone.
    lv_obj_t *inner = circle(root, INNER_SIZE, COLOR_BG_DARK);
    lv_obj_align_to(inner, ring, LV_ALIGN_CENTER, 0, 0);

    mic_icon = lv_label_create(root);
    lv_label_set_text(mic_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(mic_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(mic_icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align_to(mic_icon, ring, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < BAR_COUNT; i++) {
        bars[i] = lv_obj_create(root);
        lv_obj_remove_style_all(bars[i]);
        lv_obj_set_size(bars[i], BAR_WIDTH, BAR_MIN_HEIGHT);
        lv_obj_set_style_radius(bars[i], BAR_WIDTH / 2, 0);
        lv_obj_set_style_bg_color(bars[i], COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
        levels[i] = 0;
    }

    spinner = lv_spinner_create(root, 1000, 90);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_align_to(spinner, ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, COLOR_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 8, LV_PART_INDICATOR);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    heard_label = text_label(root, bug_font_small, COLOR_TEXT_SECONDARY, HEARD_TOP, HEARD_HEIGHT);
    answer_label = text_label(root, bug_font_body, COLOR_TEXT_PRIMARY, ANSWER_TOP, ANSWER_HEIGHT);

    ui_home_set_mode(UI_HOME_STANDBY);
    return root;
}

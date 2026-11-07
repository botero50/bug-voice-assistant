#include "ui_nav.h"
#include "assistant.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_text_entry.h"
#include "ui_wifi.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_NAV";

#define TRANSITION_MS 250
#define CLICK_BLOCK_MS 500

static const ui_screen_id_t cycle[] = {UI_SCREEN_HOME, UI_SCREEN_EMAIL, UI_SCREEN_CALENDAR, UI_SCREEN_SKILLS};
#define CYCLE_COUNT ((int)(sizeof(cycle) / sizeof(cycle[0])))

static ui_screen_id_t current = UI_SCREEN_HOME;
static uint32_t last_gesture_ms = 0;
static uint32_t transition_end_ms = 0;
static bool started = false;

static int cycle_position(ui_screen_id_t id) {
    for (int i = 0; i < CYCLE_COUNT; i++) {
        if (cycle[i] == id) return i;
    }
    return -1;
}

static lv_obj_t *build_placeholder(const char *symbol, const char *title) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *icon = lv_label_create(screen);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, COLOR_ACCENT_GREEN, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *name = lv_label_create(screen);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, bug_font_body, 0);
    lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t *hint = lv_label_create(screen);
    lv_label_set_text(hint, "Próximamente");
    lv_obj_set_style_text_font(hint, bug_font_small, 0);
    lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 28);
    return screen;
}

static lv_obj_t *build_history(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Historial");
    lv_obj_set_style_text_font(title, bug_font_body, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 74);

    char heard[128], reply[256];
    if (!assistant_history_get(0, heard, sizeof(heard), reply, sizeof(reply))) {
        lv_obj_t *empty = lv_label_create(screen);
        lv_label_set_text(empty, "Aún no hay comandos");
        lv_obj_set_style_text_font(empty, bug_font_small, 0);
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SECONDARY, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
        return screen;
    }

    // Each entry is two short lines: what was asked (gray), then the answer.
    for (int i = 0; i < ASSISTANT_HISTORY_SIZE; i++) {
        if (!assistant_history_get(i, heard, sizeof(heard), reply, sizeof(reply))) break;

        lv_obj_t *question = lv_label_create(screen);
        lv_obj_set_width(question, 290);
        lv_label_set_long_mode(question, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(question, "“%s”", heard);
        lv_obj_set_style_text_font(question, bug_font_small, 0);
        lv_obj_set_style_text_color(question, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_align(question, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 108 + i * 56);

        lv_obj_t *answer = lv_label_create(screen);
        lv_obj_set_width(answer, 290);
        lv_label_set_long_mode(answer, LV_LABEL_LONG_DOT);
        lv_label_set_text(answer, reply);
        lv_obj_set_style_text_font(answer, bug_font_small, 0);
        lv_obj_set_style_text_color(answer, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_align(answer, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(answer, LV_ALIGN_TOP_MID, 0, 108 + i * 56 + 24);
    }
    return screen;
}

static lv_obj_t *build(ui_screen_id_t id) {
    switch (id) {
    case UI_SCREEN_HOME:
        return ui_home_build();
    case UI_SCREEN_EMAIL:
        return build_placeholder(LV_SYMBOL_ENVELOPE, "Correo");
    case UI_SCREEN_CALENDAR:
        return build_placeholder(LV_SYMBOL_LIST, "Calendario");
    case UI_SCREEN_SKILLS:
        return build_placeholder(LV_SYMBOL_PLAY, "Habilidades");
    case UI_SCREEN_SETTINGS:
        return ui_settings_build();
    case UI_SCREEN_HISTORY:
        return build_history();
    case UI_SCREEN_WIFI:
        return ui_wifi_build();
    default:
        return NULL;
    }
}

// Small dots under the content show which of the swipeable screens is open.
static void add_page_dots(lv_obj_t *screen, int position) {
    const int size = 7, gap = 9;
    int total = CYCLE_COUNT * size + (CYCLE_COUNT - 1) * gap;
    for (int i = 0; i < CYCLE_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(screen);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, size, size);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, i == position ? COLOR_ACCENT_GREEN : COLOR_ACCENT_GRAY, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_TOP_MID, -total / 2 + size / 2 + i * (size + gap), 384);
    }
}

static void swipe_cb(lv_event_t *e);

void ui_nav_load_screen(lv_obj_t *screen, ui_screen_id_t id, lv_scr_load_anim_t anim) {
    lv_obj_set_style_bg_color(screen, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, swipe_cb, LV_EVENT_GESTURE, NULL);

    int position = cycle_position(id);
    if (position >= 0) {
        add_page_dots(screen, position);
    }

    uint32_t duration = anim == LV_SCR_LOAD_ANIM_NONE ? 0 : TRANSITION_MS;
    transition_end_ms = lv_tick_get() + duration;
    current = id;
    lv_scr_load_anim(screen, anim, duration, 0, true);
}

void ui_nav_go(ui_screen_id_t id, lv_scr_load_anim_t anim) {
    if (started && id == current) {
        return;
    }
    lv_obj_t *screen = build(id);
    if (!screen) {
        ESP_LOGW(TAG, "No screen for id %d", id);
        return;
    }
    started = true;
    ui_nav_load_screen(screen, id, anim);
}

void ui_nav_start(void) {
    started = false;
    ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_NONE);
}

ui_screen_id_t ui_nav_current(void) {
    return current;
}

void ui_nav_show_home_for_voice(void) {
    if (current != UI_SCREEN_HOME && current != UI_SCREEN_TEXT_ENTRY && current != UI_SCREEN_WIFI) {
        ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_NONE);
    }
}

bool ui_nav_click_allowed(void) {
    return lv_tick_elaps(last_gesture_ms) > CLICK_BLOCK_MS;
}

static void swipe(lv_dir_t dir) {
    int position = cycle_position(current);
    if (position >= 0) {
        if (dir == LV_DIR_LEFT) {
            ui_nav_go(cycle[(position + 1) % CYCLE_COUNT], LV_SCR_LOAD_ANIM_MOVE_LEFT);
        } else if (dir == LV_DIR_RIGHT) {
            ui_nav_go(cycle[(position + CYCLE_COUNT - 1) % CYCLE_COUNT], LV_SCR_LOAD_ANIM_MOVE_RIGHT);
        } else if (dir == LV_DIR_TOP) {
            if (current == UI_SCREEN_HOME) {
                ui_nav_go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_TOP);
            } else {
                ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP);
            }
        } else if (dir == LV_DIR_BOTTOM && current == UI_SCREEN_HOME) {
            ui_nav_go(UI_SCREEN_HISTORY, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
        }
        return;
    }

    switch (current) {
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_HISTORY:
        if (dir == LV_DIR_TOP) {
            ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP);
        }
        break;
    case UI_SCREEN_WIFI:
        if (dir == LV_DIR_RIGHT) {
            ui_nav_go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
        }
        break;
    case UI_SCREEN_TEXT_ENTRY:
        if (dir == LV_DIR_RIGHT) {
            ui_text_entry_cancel();
        }
        break;
    default:
        break;
    }
}

static void swipe_cb(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    last_gesture_ms = lv_tick_get();
    if (lv_tick_get() < transition_end_ms) {
        return;  // still animating
    }
    swipe(dir);
}

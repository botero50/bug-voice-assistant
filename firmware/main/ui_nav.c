#include "ui_nav.h"
#include "assistant.h"
#include "bug_fonts.h"
#include "ui_calendar.h"
#include "ui_config.h"
#include "ui_email.h"
#include "alarms.h"
#include "ui_alarm.h"
#include "ui_article.h"
#include "ui_headlines.h"
#include "ui_home.h"
#include "ui_music.h"
#include "ui_rows.h"
#include "ui_settings.h"
#include "ui_text_entry.h"
#include "ui_weather.h"
#include "ui_wifi.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_NAV";

#define TRANSITION_MS 250
#define CLICK_BLOCK_MS 500
#define IDLE_RETURN_MS 60000  // a screen left alone this long goes back Home

static ui_screen_id_t current = UI_SCREEN_HOME;
static uint32_t last_gesture_ms = 0;
static uint32_t transition_end_ms = 0;
static uint32_t opened_ms = 0;
static bool started = false;

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
        return ui_email_list_build();
    case UI_SCREEN_EMAIL_DETAIL:
        return ui_email_detail_build();
    case UI_SCREEN_CALENDAR:
        return ui_calendar_build();
    case UI_SCREEN_WEATHER:
        return ui_weather_build();
    case UI_SCREEN_MUSIC:
        return ui_music_build();
    case UI_SCREEN_ROWS:
        return ui_rows_build();
    case UI_SCREEN_HEADLINES:
        return ui_headlines_build();
    case UI_SCREEN_ARTICLE:
        return ui_article_build();
    case UI_SCREEN_ALARM:
        return ui_alarm_build();
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

static void swipe_cb(lv_event_t *e);

void ui_nav_load_screen(lv_obj_t *screen, ui_screen_id_t id, lv_scr_load_anim_t anim) {
    lv_obj_set_style_bg_color(screen, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, swipe_cb, LV_EVENT_GESTURE, NULL);

    uint32_t duration = anim == LV_SCR_LOAD_ANIM_NONE ? 0 : TRANSITION_MS;
    transition_end_ms = lv_tick_get() + duration;
    opened_ms = lv_tick_get();
    current = id;
    lv_scr_load_anim(screen, anim, duration, 0, true);
}

static void go(ui_screen_id_t id, lv_scr_load_anim_t anim, bool reload) {
    if (started && id == current && !reload) {
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

void ui_nav_go(ui_screen_id_t id, lv_scr_load_anim_t anim) {
    go(id, anim, false);
}

void ui_nav_start(void) {
    started = false;
    go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_NONE, false);
}

ui_screen_id_t ui_nav_current(void) {
    return current;
}

void ui_nav_open_screen(const char *path, const char *json) {
    if (strcmp(path, "home") == 0) {
        go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_FADE_IN, false);
        return;
    }
    if (!json) {
        return;
    }
    // Opening a screen replaces whatever is showing, even a screen of the same kind
    // (asking for the emails again refreshes them).
    if (strncmp(path, "email_detail", 12) == 0) {
        if (ui_email_set_detail(json)) go(UI_SCREEN_EMAIL_DETAIL, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "email", 5) == 0) {
        if (ui_email_set_list(json)) go(UI_SCREEN_EMAIL, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "calendar", 8) == 0) {
        if (ui_calendar_set_data(json)) go(UI_SCREEN_CALENDAR, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "weather", 7) == 0) {
        if (ui_weather_set_data(json)) go(UI_SCREEN_WEATHER, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "music", 5) == 0) {
        if (ui_music_set_data(json)) go(UI_SCREEN_MUSIC, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "rows", 4) == 0) {
        if (ui_rows_set_data(json)) go(UI_SCREEN_ROWS, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "headlines", 9) == 0) {
        if (ui_headlines_set_data(json)) go(UI_SCREEN_HEADLINES, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else if (strncmp(path, "article", 7) == 0) {
        if (ui_article_set_data(json)) go(UI_SCREEN_ARTICLE, LV_SCR_LOAD_ANIM_FADE_IN, true);
    } else {
        ESP_LOGW(TAG, "Unknown screen %s", path);
    }
}

void ui_nav_open_alarm(const char *label) {
    ui_alarm_set_label(label);
    go(UI_SCREEN_ALARM, LV_SCR_LOAD_ANIM_FADE_IN, true);
}

void ui_nav_show_home_for_voice(void) {
    if (current != UI_SCREEN_HOME && current != UI_SCREEN_TEXT_ENTRY && current != UI_SCREEN_WIFI) {
        go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_NONE, false);
    }
}

void ui_nav_keep_alive(void) {
    opened_ms = lv_tick_get();
}

void ui_nav_tick(void) {
    bool leaves_alone = current == UI_SCREEN_EMAIL || current == UI_SCREEN_EMAIL_DETAIL ||
                        current == UI_SCREEN_CALENDAR || current == UI_SCREEN_WEATHER ||
                        current == UI_SCREEN_ROWS || current == UI_SCREEN_HEADLINES ||
                        current == UI_SCREEN_ARTICLE || current == UI_SCREEN_SETTINGS ||
                        current == UI_SCREEN_HISTORY;
    if (leaves_alone && lv_tick_elaps(opened_ms) > IDLE_RETURN_MS && lv_disp_get_inactive_time(NULL) > IDLE_RETURN_MS) {
        go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_FADE_IN, false);
    }
}

bool ui_nav_click_allowed(void) {
    return lv_tick_elaps(last_gesture_ms) > CLICK_BLOCK_MS;
}

static void swipe(lv_dir_t dir) {
    switch (current) {
    case UI_SCREEN_HOME:
        if (dir == LV_DIR_TOP) {
            go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_TOP, false);
        } else if (dir == LV_DIR_BOTTOM) {
            go(UI_SCREEN_HISTORY, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, false);
        }
        break;
    case UI_SCREEN_EMAIL:
    case UI_SCREEN_CALENDAR:
    case UI_SCREEN_WEATHER:
    case UI_SCREEN_MUSIC:  // the music keeps playing; Home shows what is on
    case UI_SCREEN_ROWS:
    case UI_SCREEN_HEADLINES:
    case UI_SCREEN_ARTICLE:
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_HISTORY:
        if (dir == LV_DIR_TOP) {
            go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP, false);
        }
        break;
    case UI_SCREEN_EMAIL_DETAIL:
        if (dir == LV_DIR_TOP) {
            go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP, false);
        } else if (dir == LV_DIR_RIGHT) {
            go(ui_email_has_list() ? UI_SCREEN_EMAIL : UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_MOVE_RIGHT, false);
        }
        break;
    case UI_SCREEN_WIFI:
        if (dir == LV_DIR_RIGHT) {
            go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_RIGHT, false);
        }
        break;
    case UI_SCREEN_TEXT_ENTRY:
        if (dir == LV_DIR_RIGHT) {
            ui_text_entry_cancel();
        }
        break;
    case UI_SCREEN_ALARM:
        if (dir == LV_DIR_TOP) {
            alarm_ring_stop();
            go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP, false);
        }
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

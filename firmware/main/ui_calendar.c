#include "ui_calendar.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_CALENDAR";

#define MAX_DAYS 2
#define MAX_EVENTS 4
#define LIST_TOP 76
#define LIST_BOTTOM 346     // the round screen narrows below this
#define HEADER_HEIGHT 28
#define EVENT_HEIGHT 48
#define FOOTER_TOP 352

typedef struct {
    char time[16];
    char title[64];
    char place[40];
} event_t;

typedef struct {
    char label[64];
    event_t events[MAX_EVENTS];
    int count;
} day_t;

static day_t days[MAX_DAYS];
static int day_count = 0;
static bool is_sample = false;

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

bool ui_calendar_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Calendar JSON could not be read");
        return false;
    }
    cJSON *list = cJSON_GetObjectItem(root, "days");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return false;
    }

    day_count = 0;
    cJSON *day;
    cJSON_ArrayForEach(day, list) {
        if (day_count >= MAX_DAYS) break;
        day_t *out = &days[day_count++];
        copy_string(day, "label", out->label, sizeof(out->label));
        out->count = 0;
        cJSON *events = cJSON_GetObjectItem(day, "events");
        cJSON *event;
        cJSON_ArrayForEach(event, events) {
            if (out->count >= MAX_EVENTS) break;
            event_t *e = &out->events[out->count++];
            copy_string(event, "time", e->time, sizeof(e->time));
            copy_string(event, "title", e->title, sizeof(e->title));
            copy_string(event, "place", e->place, sizeof(e->place));
        }
    }
    is_sample = cJSON_IsTrue(cJSON_GetObjectItem(root, "sample"));
    cJSON_Delete(root);
    return true;
}

static lv_obj_t *add_label(lv_obj_t *parent, lv_color_t color, int width, int x, int y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, bug_font_small, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t *ui_calendar_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, LIST_BOTTOM - LIST_TOP);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_TOP);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_CLICKABLE);

    int y = 0;
    int limit = LIST_BOTTOM - LIST_TOP;
    for (int d = 0; d < day_count; d++) {
        if (y + HEADER_HEIGHT > limit) break;
        add_label(list, COLOR_ACCENT_GREEN, 300, 0, y, days[d].label);
        y += HEADER_HEIGHT;

        if (days[d].count == 0) {
            add_label(list, COLOR_TEXT_SECONDARY, 300, 0, y, "Sin eventos");
            y += 30;
        }
        for (int i = 0; i < days[d].count; i++) {
            if (y + EVENT_HEIGHT > limit) break;
            const event_t *e = &days[d].events[i];
            add_label(list, COLOR_TEXT_SECONDARY, 84, 0, y, e->time);
            add_label(list, COLOR_TEXT_PRIMARY, 212, 88, y, e->title);
            add_label(list, COLOR_TEXT_SECONDARY, 212, 88, y + 21, e->place);
            y += EVENT_HEIGHT;
        }
        y += 6;
    }

    if (day_count == 0) {
        lv_obj_t *empty = add_label(screen, COLOR_TEXT_SECONDARY, 280, 0, 0, "No hay eventos");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
    }

    if (is_sample) {
        lv_obj_t *note = add_label(screen, lv_color_make(110, 110, 110), 280, 0, 0, "Datos de ejemplo");
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

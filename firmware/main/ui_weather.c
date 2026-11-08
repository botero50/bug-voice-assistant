#include "ui_weather.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WEATHER";

#define MAX_DAYS 3

typedef struct {
    char day[8];
    char icon[12];
    int high;
    int low;
} forecast_day_t;

static struct {
    char city[48];
    char text[40];
    char icon[12];
    int temp;
    int high;
    int low;
    int rain;
    forecast_day_t days[MAX_DAYS];
    int day_count;
} weather;

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

static int number(cJSON *object, const char *key) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

bool ui_weather_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Weather JSON could not be read");
        return false;
    }
    copy_string(root, "city", weather.city, sizeof(weather.city));
    copy_string(root, "text", weather.text, sizeof(weather.text));
    copy_string(root, "icon", weather.icon, sizeof(weather.icon));
    weather.temp = number(root, "temp");
    weather.high = number(root, "high");
    weather.low = number(root, "low");
    weather.rain = number(root, "rain");

    weather.day_count = 0;
    cJSON *day;
    cJSON_ArrayForEach(day, cJSON_GetObjectItem(root, "days")) {
        if (weather.day_count >= MAX_DAYS) break;
        forecast_day_t *out = &weather.days[weather.day_count++];
        copy_string(day, "day", out->day, sizeof(out->day));
        copy_string(day, "icon", out->icon, sizeof(out->icon));
        out->high = number(day, "high");
        out->low = number(day, "low");
    }
    cJSON_Delete(root);
    return true;
}

// ---- Icons, drawn from simple shapes (LVGL has no weather symbols) ----

static lv_obj_t *shape(lv_obj_t *parent, int cx, int cy, int w, int h, int radius, lv_color_t color, lv_opa_t opa) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, cx - w / 2, cy - h / 2);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void draw_sun(lv_obj_t *parent, int cx, int cy, int s) {
    lv_color_t sun = lv_color_make(255, 200, 40);
    shape(parent, cx, cy, s * 8 / 10, s * 8 / 10, LV_RADIUS_CIRCLE, sun, LV_OPA_30);  // glow
    shape(parent, cx, cy, s * 5 / 10, s * 5 / 10, LV_RADIUS_CIRCLE, sun, LV_OPA_COVER);
}

static void draw_cloud(lv_obj_t *parent, int cx, int cy, int s, lv_color_t color) {
    shape(parent, cx, cy + s / 10, s * 8 / 10, s * 34 / 100, s * 17 / 100, color, LV_OPA_COVER);          // base
    shape(parent, cx - s * 16 / 100, cy - s / 50, s * 34 / 100, s * 34 / 100, LV_RADIUS_CIRCLE, color, LV_OPA_COVER);
    shape(parent, cx + s / 10, cy - s * 12 / 100, s * 42 / 100, s * 42 / 100, LV_RADIUS_CIRCLE, color, LV_OPA_COVER);
}

// kind: "clear", "partly", "cloudy", "rain", "storm" or "fog". s is the icon's size in pixels.
static void draw_icon(lv_obj_t *parent, const char *kind, int cx, int cy, int s) {
    lv_color_t cloud = lv_color_make(190, 196, 208);
    lv_color_t rain = lv_color_make(80, 150, 255);

    if (strcmp(kind, "clear") == 0) {
        draw_sun(parent, cx, cy, s);
    } else if (strcmp(kind, "partly") == 0) {
        draw_sun(parent, cx - s * 18 / 100, cy - s * 14 / 100, s * 8 / 10);
        draw_cloud(parent, cx + s / 12, cy + s / 12, s * 9 / 10, cloud);
    } else if (strcmp(kind, "rain") == 0) {
        draw_cloud(parent, cx, cy - s / 8, s, lv_color_make(140, 150, 170));
        for (int i = -1; i <= 1; i++) {
            shape(parent, cx + i * s * 18 / 100, cy + s * 30 / 100, 3 + s / 40, s * 16 / 100, 2, rain, LV_OPA_COVER);
        }
    } else if (strcmp(kind, "storm") == 0) {
        draw_cloud(parent, cx, cy - s / 8, s, lv_color_make(105, 112, 130));
        lv_obj_t *bolt = lv_label_create(parent);
        lv_label_set_text(bolt, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_font(bolt, s > 50 ? &lv_font_montserrat_28 : &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bolt, lv_color_make(255, 200, 40), 0);
        lv_obj_set_pos(bolt, cx - (s > 50 ? 8 : 5), cy + s * 14 / 100);
    } else if (strcmp(kind, "fog") == 0) {
        lv_color_t mist = lv_color_make(150, 156, 168);
        for (int i = -1; i <= 1; i++) {
            shape(parent, cx + (i == 0 ? 0 : i * s / 10), cy + i * s * 20 / 100, s * 7 / 10, 4 + s / 30, 3, mist, LV_OPA_COVER);
        }
    } else {
        draw_cloud(parent, cx, cy, s, cloud);
    }
}

static lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width, int y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

lv_obj_t *ui_weather_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    char text[64];

    add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 280, 66, weather.city);

    draw_icon(screen, weather.icon, 233, 126, 76);

    snprintf(text, sizeof(text), "%d°", weather.temp);
    add_label(screen, bug_font_huge, COLOR_TEXT_PRIMARY, 260, 160, text);

    add_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 300, 240, weather.text);

    snprintf(text, sizeof(text), "Máx %d°  ·  Mín %d°  ·  Lluvia %d%%", weather.high, weather.low, weather.rain);
    add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 320, 272, text);

    // The next days, in three columns. The screen narrows toward the bottom, so they stay close together.
    const int column_step = 92;
    for (int i = 0; i < weather.day_count; i++) {
        int cx = 233 + (i - (weather.day_count - 1) / 2) * column_step;
        const forecast_day_t *day = &weather.days[i];

        lv_obj_t *name = lv_label_create(screen);
        lv_label_set_text(name, day->day);
        lv_obj_set_style_text_font(name, bug_font_small, 0);
        lv_obj_set_style_text_color(name, COLOR_TEXT_SECONDARY, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, cx - 14, 304);

        draw_icon(screen, day->icon, cx, 346, 32);

        snprintf(text, sizeof(text), "%d° %d°", day->high, day->low);
        lv_obj_t *range = lv_label_create(screen);
        lv_label_set_text(range, text);
        lv_obj_set_style_text_font(range, bug_font_small, 0);
        lv_obj_set_style_text_color(range, COLOR_TEXT_PRIMARY, 0);
        lv_obj_align(range, LV_ALIGN_TOP_LEFT, cx - 32, 366);
    }
    return screen;
}

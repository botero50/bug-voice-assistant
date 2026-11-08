#include "ui_rows.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_ROWS";

#define MAX_ROWS 5
#define VISIBLE_ROWS 4      // the round screen has room for four; the list never scrolls
#define ROW_WIDTH 300
#define ROW_HEIGHT 56
#define ROW_TOP 100
#define ROW_STEP 62
#define FOOTER_TOP 352

typedef struct {
    char label[32];
    char note[40];
    char value[24];
    char change[16];
    int trend;  // 1 up, -1 down, 0 none
} row_t;

static char title[40] = "";
static char footer[64] = "";
static row_t rows[MAX_ROWS];
static int row_count = 0;

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

bool ui_rows_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Rows JSON could not be read");
        return false;
    }
    cJSON *list = cJSON_GetObjectItem(root, "rows");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return false;
    }

    copy_string(root, "title", title, sizeof(title));
    copy_string(root, "footer", footer, sizeof(footer));
    row_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (row_count >= MAX_ROWS) break;
        row_t *row = &rows[row_count++];
        copy_string(item, "label", row->label, sizeof(row->label));
        copy_string(item, "note", row->note, sizeof(row->note));
        copy_string(item, "value", row->value, sizeof(row->value));
        copy_string(item, "change", row->change, sizeof(row->change));
        char trend[8];
        copy_string(item, "trend", trend, sizeof(trend));
        row->trend = strcmp(trend, "up") == 0 ? 1 : (strcmp(trend, "down") == 0 ? -1 : 0);
    }
    cJSON_Delete(root);
    return true;
}

static lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_rows_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *heading = add_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 300, title);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 66);

    int shown = row_count < VISIBLE_ROWS ? row_count : VISIBLE_ROWS;
    for (int i = 0; i < shown; i++) {
        const row_t *row = &rows[i];
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, ROW_WIDTH, ROW_HEIGHT);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_bg_color(card, lv_color_make(30, 30, 30), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, ROW_TOP + i * ROW_STEP);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = add_label(card, bug_font_small, COLOR_TEXT_PRIMARY, 150, row->label);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 14, 6);

        lv_obj_t *note = add_label(card, bug_font_small, COLOR_TEXT_SECONDARY, 160, row->note);
        lv_obj_align(note, LV_ALIGN_TOP_LEFT, 14, 30);

        lv_obj_t *value = add_label(card, bug_font_body, COLOR_TEXT_PRIMARY, 128, row->value);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -12, 3);

        if (row->change[0]) {
            lv_color_t color = row->trend > 0 ? COLOR_ACCENT_GREEN : (row->trend < 0 ? COLOR_ACCENT_RED : COLOR_TEXT_SECONDARY);
            lv_obj_t *change = add_label(card, bug_font_small, color, 100, "");
            lv_label_set_text_fmt(change, "%s%s%s", row->trend > 0 ? LV_SYMBOL_UP " " : (row->trend < 0 ? LV_SYMBOL_DOWN " " : ""),
                                  row->change, "");
            lv_obj_set_style_text_align(change, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(change, LV_ALIGN_TOP_RIGHT, -12, 31);
        }
    }

    if (row_count == 0) {
        lv_obj_t *empty = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 280, "Sin datos");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
    }

    if (footer[0]) {
        lv_obj_t *note = add_label(screen, bug_font_small, lv_color_make(110, 110, 110), 290, footer);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

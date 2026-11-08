#include "ui_headlines.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_HEADLINES";

#define MAX_ITEMS 5
#define VISIBLE_ITEMS 4
#define CARD_WIDTH 300
#define CARD_HEIGHT 64
#define CARD_TOP 98
#define CARD_STEP 68
#define FOOTER_TOP 372

typedef struct {
    char title[160];
    char source[40];
} item_t;

static char title[40] = "";
static char footer[64] = "";
static item_t items[MAX_ITEMS];
static int item_count = 0;

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

bool ui_headlines_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Headlines JSON could not be read");
        return false;
    }
    cJSON *list = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return false;
    }

    copy_string(root, "title", title, sizeof(title));
    copy_string(root, "footer", footer, sizeof(footer));
    item_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (item_count >= MAX_ITEMS) break;
        item_t *out = &items[item_count++];
        copy_string(item, "title", out->title, sizeof(out->title));
        copy_string(item, "source", out->source, sizeof(out->source));
    }
    cJSON_Delete(root);
    return true;
}

static lv_obj_t *add_label(lv_obj_t *parent, lv_color_t color, int width, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, bug_font_small, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_headlines_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *heading = lv_label_create(screen);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, bug_font_body, 0);
    lv_obj_set_style_text_color(heading, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 64);

    int shown = item_count < VISIBLE_ITEMS ? item_count : VISIBLE_ITEMS;
    for (int i = 0; i < shown; i++) {
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, CARD_WIDTH, CARD_HEIGHT);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_bg_color(card, lv_color_make(30, 30, 30), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD_TOP + i * CARD_STEP);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

        // Two lines for the headline; a longer one ends in "..."
        lv_obj_t *headline = add_label(card, COLOR_TEXT_PRIMARY, CARD_WIDTH - 24, items[i].title);
        lv_obj_set_height(headline, 40);
        lv_obj_align(headline, LV_ALIGN_TOP_LEFT, 12, 4);

        lv_obj_t *source = add_label(card, lv_color_make(130, 130, 130), CARD_WIDTH - 24, items[i].source);
        lv_obj_align(source, LV_ALIGN_TOP_LEFT, 12, 42);
    }

    if (item_count == 0) {
        lv_obj_t *empty = add_label(screen, COLOR_TEXT_SECONDARY, 280, "Sin noticias");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
    }

    if (footer[0]) {
        lv_obj_t *note = add_label(screen, lv_color_make(110, 110, 110), 290, footer);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

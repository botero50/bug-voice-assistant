#include "ui_article.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_ARTICLE";

#define TEXT_WIDTH 300
#define FOOTER_TOP 352

static char title[80] = "";
static char text[420] = "";
static char footer[40] = "";

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

bool ui_article_set_data(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Article JSON could not be read");
        return false;
    }
    copy_string(root, "title", title, sizeof(title));
    copy_string(root, "text", text, sizeof(text));
    copy_string(root, "footer", footer, sizeof(footer));
    cJSON_Delete(root);
    return true;
}

lv_obj_t *ui_article_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    // A joke is short: bigger text, centered. An encyclopedia answer is longer: smaller, left-aligned.
    bool short_text = strcmp(title, "Chiste") == 0;

    lv_obj_t *heading = lv_label_create(screen);
    lv_obj_set_size(heading, TEXT_WIDTH, short_text ? 30 : 56);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, bug_font_body, 0);
    lv_obj_set_style_text_color(heading, short_text ? COLOR_ACCENT_GREEN : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 66);

    lv_obj_t *body = lv_label_create(screen);
    lv_obj_set_size(body, TEXT_WIDTH, short_text ? 200 : 214);
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    lv_label_set_text(body, text);
    lv_obj_set_style_text_font(body, short_text ? bug_font_body : bug_font_small, 0);
    lv_obj_set_style_text_color(body, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(body, short_text ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, short_text ? 120 : 128);

    if (footer[0]) {
        lv_obj_t *note = lv_label_create(screen);
        lv_obj_set_width(note, 280);
        lv_label_set_text(note, footer);
        lv_obj_set_style_text_font(note, bug_font_small, 0);
        lv_obj_set_style_text_color(note, lv_color_make(110, 110, 110), 0);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

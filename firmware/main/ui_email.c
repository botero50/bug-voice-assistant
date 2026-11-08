#include "ui_email.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_EMAIL";

#define MAX_EMAILS 6
#define VISIBLE_EMAILS 4   // the list never scrolls, so swiping up always means "back"
#define CARD_WIDTH 300
#define CARD_HEIGHT 58
#define CARD_TOP 98
#define CARD_STEP 62
#define FOOTER_TOP 352

typedef struct {
    char from[48];
    char subject[96];
    char time[16];
    char body[420];
    bool unread;
} email_t;

static email_t emails[MAX_EMAILS];
static int email_count = 0;
static int unread_count = 0;
static bool list_is_sample = false;

static email_t detail;
static bool detail_is_sample = false;

static void copy_string(cJSON *object, const char *key, char *out, size_t size) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    strlcpy(out, cJSON_IsString(item) && item->valuestring ? item->valuestring : "", size);
}

static void read_email(cJSON *object, email_t *out) {
    copy_string(object, "from", out->from, sizeof(out->from));
    copy_string(object, "subject", out->subject, sizeof(out->subject));
    copy_string(object, "time", out->time, sizeof(out->time));
    copy_string(object, "body", out->body, sizeof(out->body));
    out->unread = cJSON_IsTrue(cJSON_GetObjectItem(object, "unread"));
}

bool ui_email_set_list(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Email list JSON could not be read");
        return false;
    }
    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }

    email_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (email_count >= MAX_EMAILS) break;
        read_email(item, &emails[email_count++]);
    }
    cJSON *unread = cJSON_GetObjectItem(root, "unread");
    unread_count = cJSON_IsNumber(unread) ? unread->valueint : 0;
    list_is_sample = cJSON_IsTrue(cJSON_GetObjectItem(root, "sample"));
    cJSON_Delete(root);
    return true;
}

bool ui_email_set_detail(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Email JSON could not be read");
        return false;
    }
    read_email(root, &detail);
    detail_is_sample = cJSON_IsTrue(cJSON_GetObjectItem(root, "sample"));
    cJSON_Delete(root);
    return true;
}

bool ui_email_has_list(void) {
    return email_count > 0;
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

static void card_clicked(lv_event_t *e) {
    if (!ui_nav_click_allowed()) {
        return;
    }
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= email_count) {
        return;
    }
    detail = emails[index];
    detail_is_sample = list_is_sample;
    ui_nav_go(UI_SCREEN_EMAIL_DETAIL, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

lv_obj_t *ui_email_list_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, bug_font_body, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    if (unread_count > 0) {
        lv_label_set_text_fmt(title, "Correo  ·  %d sin leer", unread_count);
    } else {
        lv_label_set_text(title, "Correo");
    }
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);

    if (email_count == 0) {
        lv_obj_t *empty = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 280, "No hay correos");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
    }

    int shown = email_count < VISIBLE_EMAILS ? email_count : VISIBLE_EMAILS;
    for (int i = 0; i < shown; i++) {
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, CARD_WIDTH, CARD_HEIGHT);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_bg_color(card, emails[i].unread ? lv_color_make(38, 38, 38) : lv_color_make(26, 26, 26), 0);
        lv_obj_set_style_bg_color(card, lv_color_make(60, 60, 60), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD_TOP + i * CARD_STEP);
        lv_obj_add_event_cb(card, card_clicked, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);

        if (emails[i].unread) {
            lv_obj_t *dot = lv_obj_create(card);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, COLOR_ACCENT_GREEN, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 9, 12);
        }

        lv_obj_t *from = add_label(card, bug_font_small, emails[i].unread ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY,
                                   176, emails[i].from);
        lv_obj_align(from, LV_ALIGN_TOP_LEFT, 24, 6);

        lv_obj_t *time = add_label(card, bug_font_small, COLOR_TEXT_SECONDARY, 80, emails[i].time);
        lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(time, LV_ALIGN_TOP_RIGHT, -12, 6);

        lv_obj_t *subject = add_label(card, bug_font_small, COLOR_TEXT_SECONDARY, 264, emails[i].subject);
        lv_obj_align(subject, LV_ALIGN_TOP_LEFT, 24, 30);
    }

    char footer[64] = "";
    if (email_count > VISIBLE_EMAILS) {
        snprintf(footer, sizeof(footer), "+%d más", email_count - VISIBLE_EMAILS);
    }
    if (list_is_sample) {
        strlcat(footer, footer[0] ? "  ·  Datos de ejemplo" : "Datos de ejemplo", sizeof(footer));
    }
    if (footer[0]) {
        lv_obj_t *note = add_label(screen, bug_font_small, lv_color_make(110, 110, 110), 280, footer);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

lv_obj_t *ui_email_detail_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *from = add_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 300, detail.from);
    lv_obj_set_style_text_align(from, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(from, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *time = add_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 300, detail.time);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time, LV_ALIGN_TOP_MID, 0, 100);

    lv_obj_t *subject = add_label(screen, bug_font_small, COLOR_ACCENT_GREEN, 300, detail.subject);
    lv_label_set_long_mode(subject, LV_LABEL_LONG_DOT);
    lv_obj_set_height(subject, 42);
    lv_obj_set_style_text_align(subject, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(subject, LV_ALIGN_TOP_MID, 0, 124);

    lv_obj_t *body = add_label(screen, bug_font_small, COLOR_TEXT_PRIMARY, 300, detail.body);
    lv_obj_set_height(body, 170);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 174);

    if (detail_is_sample) {
        lv_obj_t *note = add_label(screen, bug_font_small, lv_color_make(110, 110, 110), 280, "Datos de ejemplo");
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, FOOTER_TOP);
    }
    return screen;
}

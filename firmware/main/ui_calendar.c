#include "ui_calendar.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_CALENDAR";

lv_obj_t *ui_create_calendar_screen(void) {
    ESP_LOGI(TAG, "Creating calendar screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header: "📅 Today"
    lv_obj_t *header = lv_label_create(container);
    lv_label_set_text(header, "📅 Today");
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_text_color(header, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    // Events list
    lv_obj_t *list = lv_obj_create(container);
    lv_obj_set_size(list, 390, 310);
    lv_obj_set_pos(list, 5, 35);
    lv_obj_set_style_bg_opa(list, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    // Today's events
    const char *events_today[] = {
        "4:00 PM Team Standup",
        "6:30 PM Dentist appt"
    };
    const char *event_details[] = {
        "(Join now?)",
        "(Downtown)"
    };

    int y_pos = 0;
    for (int i = 0; i < 2; i++) {
        // Event card
        lv_obj_t *card = lv_obj_create(list);
        lv_obj_set_size(card, 380, 50);
        lv_obj_set_pos(card, 0, y_pos);
        lv_obj_set_style_bg_color(card, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
        lv_obj_set_style_border_opa(card, LV_OPA_0, 0);
        lv_obj_set_style_pad_all(card, 8, 0);

        // Event time + title
        lv_obj_t *event_label = lv_label_create(card);
        lv_label_set_text(event_label, events_today[i]);
        lv_obj_set_pos(event_label, 5, 5);
        lv_obj_set_style_text_color(event_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(event_label, &lv_font_montserrat_14, 0);

        // Event details
        lv_obj_t *detail_label = lv_label_create(card);
        lv_label_set_text(detail_label, event_details[i]);
        lv_obj_set_pos(detail_label, 5, 25);
        lv_obj_set_style_text_color(detail_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_12, 0);

        y_pos += 60;
    }

    // Tomorrow section
    y_pos += 20;

    lv_obj_t *tomorrow_label = lv_label_create(list);
    lv_label_set_text(tomorrow_label, "Tomorrow:");
    lv_obj_set_pos(tomorrow_label, 0, y_pos);
    lv_obj_set_style_text_color(tomorrow_label, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_text_font(tomorrow_label, &lv_font_montserrat_12, 0);

    y_pos += 25;

    // Tomorrow's events
    const char *events_tomorrow[] = {
        "9:00 AM All-hands",
        "2:00 PM 1:1 Manager"
    };

    for (int i = 0; i < 2; i++) {
        lv_obj_t *card = lv_obj_create(list);
        lv_obj_set_size(card, 380, 45);
        lv_obj_set_pos(card, 0, y_pos);
        lv_obj_set_style_bg_color(card, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);  // More transparent for future events
        lv_obj_set_style_border_opa(card, LV_OPA_0, 0);
        lv_obj_set_style_pad_all(card, 8, 0);

        lv_obj_t *event = lv_label_create(card);
        lv_label_set_text(event, events_tomorrow[i]);
        lv_obj_set_pos(event, 5, 10);
        lv_obj_set_style_text_color(event, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(event, &lv_font_montserrat_14, 0);

        y_pos += 50;
    }

    ESP_LOGI(TAG, "Calendar screen created");

    return root;
}

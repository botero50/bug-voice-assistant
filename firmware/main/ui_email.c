#include "ui_email.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_EMAIL";

// Mock email data for demo
static const struct {
    const char *sender;
    const char *subject;
    const char *time;
    bool unread;
} mock_emails[] = {
    {"Alex Jones", "Project update deadline", "2:30 pm", true},
    {"Manager", "Important updates needed", "12:15 pm", false},
    {"Support Team", "Password reset request", "10:45 am", false},
};

#define MOCK_EMAIL_COUNT 3

lv_obj_t *ui_create_email_list_screen(void) {
    ESP_LOGI(TAG, "Creating email list screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header: "Email" + count
    lv_obj_t *header = lv_obj_create(container);
    lv_obj_set_size(header, 390, 30);
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_bg_opa(header, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_0, 0);

    lv_obj_t *header_label = lv_label_create(header);
    lv_label_set_text(header_label, "✉ 3 new");
    lv_obj_set_style_text_color(header_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header_label, &lv_font_montserrat_18, 0);
    lv_obj_align(header_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Email list (scrollable)
    lv_obj_t *list = lv_obj_create(container);
    lv_obj_set_size(list, 390, 280);
    lv_obj_set_pos(list, 5, 40);
    lv_obj_set_style_bg_opa(list, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    // Email cards
    for (int i = 0; i < MOCK_EMAIL_COUNT; i++) {
        // Card container
        lv_obj_t *card = lv_obj_create(list);
        lv_obj_set_size(card, 380, 65);
        lv_obj_set_pos(card, 0, i * 70);
        lv_obj_set_style_bg_color(card, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_bg_opa(card, i == 0 ? LV_OPA_20 : LV_OPA_10, 0);
        lv_obj_set_style_border_color(card, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_radius(card, 4, 0);

        // Sender name
        lv_obj_t *sender = lv_label_create(card);
        lv_label_set_text(sender, mock_emails[i].sender);
        lv_obj_set_pos(sender, 5, 5);
        lv_obj_set_style_text_color(sender, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(sender, &lv_font_montserrat_14, 0);

        // Unread indicator (if applicable)
        if (mock_emails[i].unread) {
            lv_obj_t *unread_dot = lv_label_create(card);
            lv_label_set_text(unread_dot, "◆");
            lv_obj_set_pos(unread_dot, 350, 8);
            lv_obj_set_style_text_color(unread_dot, COLOR_ACCENT_GREEN, 0);
            lv_obj_set_style_text_font(unread_dot, &lv_font_montserrat_12, 0);
        }

        // Subject preview
        lv_obj_t *subject = lv_label_create(card);
        lv_label_set_text(subject, mock_emails[i].subject);
        lv_obj_set_width(subject, 340);
        lv_label_set_long_mode(subject, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(subject, 5, 22);
        lv_obj_set_style_text_color(subject, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(subject, &lv_font_montserrat_12, 0);

        // Time
        lv_obj_t *time = lv_label_create(card);
        lv_label_set_text(time, mock_emails[i].time);
        lv_obj_set_pos(time, 5, 40);
        lv_obj_set_style_text_color(time, COLOR_ACCENT_GRAY, 0);
        lv_obj_set_style_text_font(time, &lv_font_montserrat_10, 0);
    }

    // Bottom navigation
    lv_obj_t *nav = lv_obj_create(container);
    lv_obj_set_size(nav, 390, 40);
    lv_obj_set_pos(nav, 5, 330);
    lv_obj_set_style_bg_opa(nav, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(nav, LV_OPA_0, 0);

    lv_obj_t *nav_label = lv_label_create(nav);
    lv_label_set_text(nav_label, "Swipe: select | Voice: read emails");
    lv_obj_set_style_text_color(nav_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(nav_label, &lv_font_montserrat_10, 0);
    lv_obj_center(nav_label);

    ESP_LOGI(TAG, "Email list screen created");

    return root;
}

lv_obj_t *ui_create_email_detail_screen(const char *from, const char *subject, const char *body) {
    ESP_LOGI(TAG, "Creating email detail screen");

    lv_obj_t *root = lv_scr_act();

    // Main container
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 380);
    lv_obj_set_pos(container, 33, 40);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header: From, Subject, Time
    lv_obj_t *from_label = lv_label_create(container);
    lv_label_set_text(from_label, from ? from : "Unknown Sender");
    lv_obj_set_pos(from_label, 5, 5);
    lv_obj_set_style_text_color(from_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(from_label, &lv_font_montserrat_14, 0);

    lv_obj_t *subject_label = lv_label_create(container);
    lv_label_set_text(subject_label, subject ? subject : "No Subject");
    lv_obj_set_width(subject_label, 380);
    lv_label_set_long_mode(subject_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(subject_label, 5, 25);
    lv_obj_set_style_text_color(subject_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(subject_label, &lv_font_montserrat_12, 0);

    lv_obj_t *time_label = lv_label_create(container);
    lv_label_set_text(time_label, "2:30 pm");
    lv_obj_set_pos(time_label, 5, 45);
    lv_obj_set_style_text_color(time_label, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_10, 0);

    // Divider line
    lv_obj_t *divider = lv_obj_create(container);
    lv_obj_set_size(divider, 380, 1);
    lv_obj_set_pos(divider, 5, 60);
    lv_obj_set_style_bg_color(divider, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_20, 0);
    lv_obj_set_style_border_opa(divider, LV_OPA_0, 0);

    // Email body (scrollable)
    lv_obj_t *body_text = lv_label_create(container);
    lv_label_set_text(body_text, body ? body : "No content");
    lv_obj_set_width(body_text, 370);
    lv_label_set_long_mode(body_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(body_text, 10, 75);
    lv_obj_set_style_text_color(body_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(body_text, &lv_font_montserrat_13, 0);

    // Action buttons
    lv_obj_t *btn_reply = lv_btn_create(container);
    lv_obj_set_size(btn_reply, 100, 35);
    lv_obj_set_pos(btn_reply, 20, 330);
    lv_obj_set_style_bg_color(btn_reply, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(btn_reply, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_reply, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(btn_reply, 1, 0);

    lv_obj_t *reply_label = lv_label_create(btn_reply);
    lv_label_set_text(reply_label, "↶ Reply");
    lv_obj_center(reply_label);
    lv_obj_set_style_text_color(reply_label, COLOR_ACCENT_GREEN, 0);

    lv_obj_t *btn_delete = lv_btn_create(container);
    lv_obj_set_size(btn_delete, 100, 35);
    lv_obj_set_pos(btn_delete, 280, 330);
    lv_obj_set_style_bg_color(btn_delete, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(btn_delete, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_delete, COLOR_ACCENT_RED, 0);
    lv_obj_set_style_border_width(btn_delete, 1, 0);

    lv_obj_t *delete_label = lv_label_create(btn_delete);
    lv_label_set_text(delete_label, "Delete");
    lv_obj_center(delete_label);
    lv_obj_set_style_text_color(delete_label, COLOR_ACCENT_RED, 0);

    ESP_LOGI(TAG, "Email detail screen created");

    return root;
}

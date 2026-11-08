#include "ui_text_entry.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_TEXT";

// A 6-column keyboard: on this 1.75" screen a phone-style 10-column keyboard
// would have keys about 3 mm wide, too small for a finger.
#define SHIFT LV_SYMBOL_UP
#define BACKSPACE LV_SYMBOL_BACKSPACE
#define ACCEPT LV_SYMBOL_OK
#define SPACE "espacio"

static const char *LOWER_MAP[] = {
    "a", "b", "c", "d", "e", "f", "\n",
    "g", "h", "i", "j", "k", "l", "\n",
    "m", "n", "o", "p", "q", "r", "\n",
    "s", "t", "u", "v", "w", "x", "\n",
    "y", "z", SHIFT, "123", BACKSPACE, ACCEPT, ""};

static const char *UPPER_MAP[] = {
    "A", "B", "C", "D", "E", "F", "\n",
    "G", "H", "I", "J", "K", "L", "\n",
    "M", "N", "O", "P", "Q", "R", "\n",
    "S", "T", "U", "V", "W", "X", "\n",
    "Y", "Z", SHIFT, "123", BACKSPACE, ACCEPT, ""};

static const char *SYMBOLS1_MAP[] = {
    "1", "2", "3", "4", "5", "6", "\n",
    "7", "8", "9", "0", "-", "_", "\n",
    ".", ":", ",", "!", "?", "@", "\n",
    "$", "%", "&", "*", "+", "=", "\n",
    "abc", LV_SYMBOL_RIGHT, SPACE, BACKSPACE, ACCEPT, ""};

static const char *SYMBOLS2_MAP[] = {
    "(", ")", "[", "]", "{", "}", "\n",
    "<", ">", "^", "~", "|", "\\", "\n",
    "'", "\"", "`", ";", "/", "#", "\n",
    "1", "2", "3", "4", "5", "6", "\n",
    "abc", LV_SYMBOL_LEFT, SPACE, BACKSPACE, ACCEPT, ""};

// One entry per key: its width in columns. Keys default to 1 wide.
static const lv_btnmatrix_ctrl_t SYMBOLS_CTRL[] = {
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 1};

typedef enum { PAGE_LOWER, PAGE_UPPER, PAGE_SYMBOLS1, PAGE_SYMBOLS2 } page_t;

#define MAX_TEXT 300
#define KEYBOARD_WIDTH 318
#define KEYBOARD_HEIGHT 236
#define VISIBLE_CHARS 22

static lv_obj_t *keyboard = NULL;
static lv_obj_t *field_label = NULL;
static char text[MAX_TEXT + 1];
static bool secret_field = false;
static bool revealed = false;
static ui_screen_id_t return_screen = UI_SCREEN_SETTINGS;
static ui_text_entry_cb done_callback = NULL;

static void refresh_field(void) {
    if (!field_label) return;

    size_t length = strlen(text);
    char shown[3 * VISIBLE_CHARS + 8];
    shown[0] = '\0';
    const char *start = text;
    if (length > VISIBLE_CHARS) {
        start = text + length - VISIBLE_CHARS;  // the end is what is being typed
        strlcat(shown, "…", sizeof(shown));
    }
    size_t visible = strlen(start);
    if (secret_field && !revealed) {
        for (size_t i = 0; i < visible; i++) {
            strlcat(shown, "•", sizeof(shown));
        }
    } else {
        strlcat(shown, start, sizeof(shown));
    }
    if (length == 0) {
        strlcpy(shown, secret_field ? "toca para ver" : "", sizeof(shown));
    }
    lv_label_set_text(field_label, shown);
    lv_obj_set_style_text_color(field_label, length == 0 ? COLOR_TEXT_SECONDARY : COLOR_TEXT_PRIMARY, 0);
}

static void show_page(page_t page) {
    // Changing the map resets every key to 1 column wide
    switch (page) {
    case PAGE_LOWER:
        lv_btnmatrix_set_map(keyboard, LOWER_MAP);
        break;
    case PAGE_UPPER:
        lv_btnmatrix_set_map(keyboard, UPPER_MAP);
        break;
    case PAGE_SYMBOLS1:
        lv_btnmatrix_set_map(keyboard, SYMBOLS1_MAP);
        lv_btnmatrix_set_ctrl_map(keyboard, SYMBOLS_CTRL);
        break;
    case PAGE_SYMBOLS2:
        lv_btnmatrix_set_map(keyboard, SYMBOLS2_MAP);
        lv_btnmatrix_set_ctrl_map(keyboard, SYMBOLS_CTRL);
        break;
    }
}

static page_t current_page(void) {
    const char *first = lv_btnmatrix_get_btn_text(keyboard, 0);
    if (!first) return PAGE_LOWER;
    if (strcmp(first, "a") == 0) return PAGE_LOWER;
    if (strcmp(first, "A") == 0) return PAGE_UPPER;
    if (strcmp(first, "1") == 0) return PAGE_SYMBOLS1;
    return PAGE_SYMBOLS2;
}

static void leave(bool accepted) {
    ui_text_entry_cb callback = done_callback;
    done_callback = NULL;
    if (callback) {
        callback(accepted, text);
    }
    if (ui_nav_current() == UI_SCREEN_TEXT_ENTRY) {
        ui_nav_go(return_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void key_cb(lv_event_t *e) {
    uint16_t id = lv_btnmatrix_get_selected_btn(keyboard);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *key = lv_btnmatrix_get_btn_text(keyboard, id);
    if (!key) return;

    if (strcmp(key, ACCEPT) == 0) {
        leave(true);
        return;
    }
    if (strcmp(key, BACKSPACE) == 0) {
        size_t length = strlen(text);
        if (length > 0) text[length - 1] = '\0';
    } else if (strcmp(key, SHIFT) == 0) {
        show_page(current_page() == PAGE_UPPER ? PAGE_LOWER : PAGE_UPPER);
    } else if (strcmp(key, "123") == 0) {
        show_page(PAGE_SYMBOLS1);
    } else if (strcmp(key, "abc") == 0) {
        show_page(PAGE_LOWER);
    } else if (strcmp(key, LV_SYMBOL_RIGHT) == 0) {
        show_page(PAGE_SYMBOLS2);
    } else if (strcmp(key, LV_SYMBOL_LEFT) == 0) {
        show_page(PAGE_SYMBOLS1);
    } else {
        size_t length = strlen(text);
        if (length < MAX_TEXT) {
            text[length] = strcmp(key, SPACE) == 0 ? ' ' : key[0];
            text[length + 1] = '\0';
        }
        if (current_page() == PAGE_UPPER) {
            show_page(PAGE_LOWER);  // a capital letter is one-shot
        }
    }
    refresh_field();
}

static void field_cb(lv_event_t *e) {
    if (secret_field && ui_nav_click_allowed()) {
        revealed = !revealed;
        refresh_field();
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    keyboard = NULL;
    field_label = NULL;
}

void ui_text_entry_open(const char *title, const char *initial, bool secret, ui_screen_id_t return_to,
                        ui_text_entry_cb callback) {
    strlcpy(text, initial ? initial : "", sizeof(text));
    secret_field = secret;
    revealed = false;
    return_screen = return_to;
    done_callback = callback;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title_label = lv_label_create(screen);
    lv_obj_set_width(title_label, 280);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, bug_font_small, 0);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *field = lv_obj_create(screen);
    lv_obj_remove_style_all(field);
    lv_obj_set_size(field, 300, 36);
    lv_obj_set_style_radius(field, 8, 0);
    lv_obj_set_style_bg_color(field, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_align(field, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_add_event_cb(field, field_cb, LV_EVENT_SHORT_CLICKED, NULL);

    field_label = lv_label_create(field);
    lv_obj_set_style_text_font(field_label, bug_font_small, 0);
    lv_obj_center(field_label);

    keyboard = lv_btnmatrix_create(screen);
    lv_obj_set_size(keyboard, KEYBOARD_WIDTH, KEYBOARD_HEIGHT);
    lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 144);
    lv_obj_set_style_pad_all(keyboard, 0, 0);
    lv_obj_set_style_pad_gap(keyboard, 4, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_make(42, 42, 42), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, COLOR_ACCENT_GREEN, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, COLOR_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
    // Addresses start with a digit, so open on the numbers page for them
    show_page(text[0] >= '0' && text[0] <= '9' ? PAGE_SYMBOLS1 : PAGE_LOWER);
    lv_obj_add_event_cb(keyboard, key_cb, LV_EVENT_VALUE_CHANGED, NULL);

    refresh_field();
    ESP_LOGI(TAG, "Keyboard opened: %s", title);
    ui_nav_load_screen(screen, UI_SCREEN_TEXT_ENTRY, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

void ui_text_entry_cancel(void) {
    leave(false);
}

#include "ui_settings.h"
#include "assistant.h"
#include "audio_driver.h"
#include "battery.h"
#include "bug_fonts.h"
#include "display_driver.h"
#include "settings_store.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "ui_text_entry.h"
#include "local_skills.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SETTINGS";

// Six rows fit between the clock and the battery without scrolling, so swiping up
// always means "go back".
#define ROW_WIDTH 300
#define ROW_HEIGHT 40
#define ROW_TOP 76
#define ROW_STEP 46
#define ROW_COLOR lv_color_make(30, 30, 30)

static lv_obj_t *make_row(lv_obj_t *parent, int index, const char *name) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_WIDTH, ROW_HEIGHT);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, ROW_COLOR, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ROW_TOP + index * ROW_STEP);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, bug_font_small, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 14, 0);
    return row;
}

static lv_obj_t *add_value_label(lv_obj_t *row, const char *text, int width) {
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, bug_font_small, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -14, 0);
    return label;
}

static lv_obj_t *add_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t callback) {
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, 130, 12);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_set_ext_click_area(slider, 14);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_make(70, 70, 70), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, COLOR_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_ALL, NULL);
    return slider;
}

static void brightness_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        display_set_brightness(value);
    } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        settings_set_brightness(value);
    }
}

static void volume_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        audio_set_volume(value);
    } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        settings_set_volume(value);
        audio_set_speaker(true);  // a short beep so the new level can be heard
        vTaskDelay(pdMS_TO_TICKS(20));
        audio_beep(660.0f, 140);
        vTaskDelay(pdMS_TO_TICKS(40));
        audio_set_speaker(false);
    }
}

static void mic_cb(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    settings_set_mic_muted(!on);
    assistant_set_muted(!on);
}

static void wifi_row_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        ui_nav_go(UI_SCREEN_WIFI, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    }
}

static void server_done(bool accepted, const char *text) {
    if (!accepted || !text[0]) {
        return;
    }
    char url[96];
    snprintf(url, sizeof(url), "http://%s%s", text, strchr(text, ':') ? "" : ":5000");
    settings_set_server_url(url);
    ESP_LOGI(TAG, "Server address set to %s", url);
}

static void server_row_cb(lv_event_t *e) {
    if (!ui_nav_click_allowed()) {
        return;
    }
    bug_settings_t settings;
    settings_copy(&settings);
    const char *address = strstr(settings.server_url, "://");
    address = address ? address + 3 : settings.server_url;
    ui_text_entry_open("Dirección del servidor (IP:puerto)", address, false, UI_SCREEN_SETTINGS, server_done);
}

static void calendar_done(bool accepted, const char *text) {
    if (!accepted) return;
    local_calendar_set_urls(text);  // empty text removes the links
}

static void calendar_row_cb(lv_event_t *e) {
    if (ui_nav_click_allowed()) {
        ui_text_entry_open("Enlace del calendario (ICS)", "", false, UI_SCREEN_SETTINGS, calendar_done);
    }
}

lv_obj_t *ui_settings_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    bug_settings_t settings;
    settings_copy(&settings);

    lv_obj_t *row = make_row(screen, 0, LV_SYMBOL_EYE_OPEN "  Brillo");
    add_slider(row, 10, 100, settings.brightness, brightness_cb);

    row = make_row(screen, 1, LV_SYMBOL_VOLUME_MAX "  Volumen");
    add_slider(row, 0, 100, settings.volume, volume_cb);

    row = make_row(screen, 2, LV_SYMBOL_AUDIO "  Micrófono");
    lv_obj_t *mic = lv_switch_create(row);
    lv_obj_set_size(mic, 50, 26);
    lv_obj_align(mic, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_bg_color(mic, COLOR_ACCENT_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (!settings.mic_muted) {
        lv_obj_add_state(mic, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(mic, mic_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = make_row(screen, 3, LV_SYMBOL_WIFI "  WiFi");
    add_value_label(row, settings.wifi_ssid[0] ? settings.wifi_ssid : "Sin red", 150);
    lv_obj_add_event_cb(row, wifi_row_cb, LV_EVENT_SHORT_CLICKED, NULL);

    row = make_row(screen, 4, "Servidor");
    const char *address = strstr(settings.server_url, "://");
    add_value_label(row, address ? address + 3 : settings.server_url, 170);
    lv_obj_add_event_cb(row, server_row_cb, LV_EVENT_SHORT_CLICKED, NULL);

    row = make_row(screen, 5, LV_SYMBOL_BATTERY_FULL "  Batería");
    battery_status_t battery;
    char battery_text[40] = "--";
    if (battery_read(&battery) && battery.percent >= 0) {
        snprintf(battery_text, sizeof(battery_text), "%d%%%s", battery.percent, battery.charging ? "  cargando" : "");
    }
    add_value_label(row, battery_text, 170);

    row = make_row(screen, 6, LV_SYMBOL_LIST "  Calendario");
    add_value_label(row, local_calendar_configured() ? "Con enlace" : "Sin enlace", 150);
    lv_obj_add_event_cb(row, calendar_row_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(screen);
    lv_label_set_text(hint, "Desliza hacia arriba para volver");
    lv_obj_set_style_text_font(hint, bug_font_small, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(110, 110, 110), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, ROW_TOP + 7 * ROW_STEP + 2);
    return screen;
}

#include "ui_wifi.h"
#include "assistant.h"
#include "bug_fonts.h"
#include "network_manager.h"
#include "settings_store.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "ui_text_entry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI";

#define MAX_NETWORKS 12
#define POLL_MS 200
#define RESULT_SHOW_MS 2200

typedef enum {
    VIEW_SCANNING,
    VIEW_LIST,
    VIEW_CONNECTING,
    VIEW_RESULT,
} view_t;

// Scan results survive screen changes (the keyboard replaces this screen).
static wifi_ap_info_t networks[MAX_NETWORKS];
static int network_count = 0;
static bool scan_valid = false;
static volatile bool scan_running = false;
static volatile bool scan_finished = false;

static volatile bool connect_running = false;
static volatile bool connect_finished = false;
static volatile bool connect_ok = false;
static volatile net_error_t connect_error = NET_ERR_NONE;
static bool connect_pending = false;
static bool from_keyboard = false;
static char pending_ssid[33];
static char pending_password[65];

static lv_obj_t *screen = NULL;
static lv_obj_t *list = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *spinner = NULL;
static lv_obj_t *rescan_button = NULL;
static lv_obj_t *back_button = NULL;
static uint32_t busy_since_ms = 0;  // when the scan or the connection began: neither may last forever
static lv_timer_t *poll_timer = NULL;
static view_t view = VIEW_SCANNING;
static uint32_t result_since_ms = 0;
static bool result_ok = false;

static void show_view(view_t next);

static void scan_task(void *arg) {
    int count = 0;
    wifi_ap_info_t found[MAX_NETWORKS];
    esp_err_t err = network_manager_scan(found, MAX_NETWORKS, &count);
    if (err == ESP_OK) {
        memcpy(networks, found, sizeof(found));
        network_count = count;
        scan_valid = true;
    } else {
        scan_valid = false;
    }
    scan_running = false;
    scan_finished = true;
    vTaskDelete(NULL);
}

static void start_scan(void) {
    if (scan_running) return;
    scan_running = true;
    scan_finished = false;
    scan_valid = false;
    busy_since_ms = lv_tick_get();
    if (xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "The Wi-Fi scan could not start (not enough memory)");
        scan_running = false;
        scan_finished = true;  // the list says there are no networks, and the way out is on the screen
    }
}

static void connect_task(void *arg) {
    bug_settings_t previous;
    settings_copy(&previous);

    esp_err_t err = network_manager_connect_wifi(pending_ssid, pending_password);
    connect_ok = err == ESP_OK;
    connect_error = network_manager_last_error();
    if (connect_ok) {
        settings_set_wifi(pending_ssid, pending_password);
        assistant_wifi_changed();
    } else if (previous.wifi_ssid[0]) {
        // Go back to the network that worked before
        network_manager_connect_wifi_async(previous.wifi_ssid, previous.wifi_password);
    } else {
        network_manager_disconnect();
    }
    connect_running = false;
    connect_finished = true;
    vTaskDelete(NULL);
}

static void start_connect(void) {
    if (connect_running) return;
    connect_running = true;
    connect_finished = false;
    busy_since_ms = lv_tick_get();
    if (xTaskCreate(connect_task, "wifi_connect", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "The Wi-Fi connection could not start (not enough memory)");
        connect_running = false;
        connect_ok = false;
        connect_error = NET_ERR_OTHER;
        connect_finished = true;
    }
}

static void password_done(bool accepted, const char *text) {
    if (!accepted) {
        return;
    }
    strlcpy(pending_password, text, sizeof(pending_password));
    connect_pending = true;
}

static void network_clicked(lv_event_t *e) {
    if (!ui_nav_click_allowed()) {
        return;
    }
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= network_count) {
        return;
    }
    strlcpy(pending_ssid, networks[index].ssid, sizeof(pending_ssid));
    pending_password[0] = '\0';

    if (!networks[index].secure) {
        show_view(VIEW_CONNECTING);  // open network, no password needed
        start_connect();
        return;
    }

    char title[64];
    snprintf(title, sizeof(title), "Contraseña de %s", networks[index].ssid);
    from_keyboard = true;  // so the list isn't scanned again when we come back
    ui_text_entry_open(title, "", true, UI_SCREEN_WIFI, password_done);
}

static void back_clicked(lv_event_t *e) {
    ui_nav_go(UI_SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

static void rescan_clicked(lv_event_t *e) {
    if (!ui_nav_click_allowed()) {
        return;
    }
    start_scan();
    show_view(VIEW_SCANNING);
}

static lv_color_t signal_color(int rssi) {
    if (rssi > -60) return COLOR_ACCENT_GREEN;
    if (rssi > -75) return lv_color_make(255, 193, 7);
    return COLOR_ACCENT_RED;
}

static void fill_list(void) {
    lv_obj_clean(list);
    for (int i = 0; i < network_count; i++) {
        lv_obj_t *button = lv_list_add_btn(list, NULL, networks[i].ssid);
        lv_obj_set_style_bg_color(button, lv_color_make(30, 30, 30), 0);
        lv_obj_set_style_bg_color(button, COLOR_ACCENT_GREEN, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(button, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(button, bug_font_small, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_height(button, 42);

        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label) {
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(label, 195);
        }

        lv_obj_t *signal = lv_label_create(button);
        lv_label_set_text(signal, networks[i].secure ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI " abierta");
        lv_obj_set_style_text_font(signal, bug_font_small, 0);
        lv_obj_set_style_text_color(signal, signal_color(networks[i].rssi), 0);
        lv_obj_align(signal, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(button, network_clicked, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
}

static const char *error_message(net_error_t error) {
    switch (error) {
    case NET_ERR_PASSWORD:
        return "Contraseña incorrecta";
    case NET_ERR_NOT_FOUND:
        return "No encuentro la red";
    default:
        return "No pude conectar";
    }
}

static void show_view(view_t next) {
    view = next;
    bool busy = next == VIEW_SCANNING || next == VIEW_CONNECTING;
    if (busy) {
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (next == VIEW_LIST) {
        lv_obj_clear_flag(list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rescan_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rescan_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    }

    switch (next) {
    case VIEW_SCANNING:
        lv_label_set_text(status_label, "Buscando redes...");
        break;
    case VIEW_CONNECTING:
        lv_label_set_text_fmt(status_label, "Conectando a\n%s...", pending_ssid);
        break;
    case VIEW_LIST:
        if (network_count == 0) {
            lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status_label, "No encontré redes");
        } else {
            fill_list();
        }
        break;
    case VIEW_RESULT:
        if (result_ok) {
            lv_label_set_text_fmt(status_label, "Conectado a\n%s", pending_ssid);
        } else {
            lv_label_set_text(status_label, error_message(connect_error));
        }
        result_since_ms = lv_tick_get();
        break;
    }
}

static void poll_cb(lv_timer_t *timer) {
    if (view == VIEW_SCANNING && !scan_finished && lv_tick_elaps(busy_since_ms) > 20000) {  // the scan never answered
        ESP_LOGW(TAG, "The Wi-Fi scan took too long");
        scan_valid = false;
        network_count = 0;
        scan_finished = true;
    }
    if (view == VIEW_CONNECTING && !connect_finished && lv_tick_elaps(busy_since_ms) > 45000) {  // the connection never answered
        ESP_LOGW(TAG, "The Wi-Fi connection took too long");
        connect_ok = false;
        connect_error = NET_ERR_OTHER;
        connect_finished = true;
    }
    if (view == VIEW_SCANNING && scan_finished) {
        scan_finished = false;
        show_view(VIEW_LIST);
    } else if (view == VIEW_CONNECTING && connect_finished) {
        connect_finished = false;
        result_ok = connect_ok;
        show_view(VIEW_RESULT);
    } else if (view == VIEW_RESULT && lv_tick_elaps(result_since_ms) > RESULT_SHOW_MS) {
        if (result_ok) {
            ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
            view = VIEW_LIST;  // stop polling this branch until the screen is gone
        } else {
            show_view(VIEW_LIST);
        }
    }
}

static void screen_deleted_cb(lv_event_t *e) {
    if (poll_timer) {
        lv_timer_del(poll_timer);
        poll_timer = NULL;
    }
    screen = NULL;
    list = NULL;
    status_label = NULL;
    spinner = NULL;
    rescan_button = NULL;
    back_button = NULL;
}

lv_obj_t *ui_wifi_build(void) {
    screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Redes WiFi");
    lv_obj_set_style_text_font(title, bug_font_small, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 72);

    list = lv_list_create(screen);
    lv_obj_set_size(list, 300, 214);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    rescan_button = lv_btn_create(screen);
    lv_obj_set_size(rescan_button, 170, 36);
    lv_obj_align(rescan_button, LV_ALIGN_TOP_MID, 0, 322);
    lv_obj_set_style_bg_color(rescan_button, lv_color_make(42, 42, 42), 0);
    lv_obj_set_style_radius(rescan_button, 18, 0);
    lv_obj_set_style_shadow_width(rescan_button, 0, 0);
    lv_obj_add_event_cb(rescan_button, rescan_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *rescan_label = lv_label_create(rescan_button);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH "  Buscar de nuevo");
    lv_obj_set_style_text_font(rescan_label, bug_font_small, 0);
    lv_obj_center(rescan_label);

    back_button = lv_btn_create(screen);
    lv_obj_set_size(back_button, 150, 34);
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, 0, 368);
    lv_obj_set_style_bg_color(back_button, lv_color_make(60, 60, 66), 0);
    lv_obj_set_style_radius(back_button, 17, 0);
    lv_obj_set_style_shadow_width(back_button, 0, 0);
    lv_obj_add_event_cb(back_button, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT "  Volver");
    lv_obj_set_style_text_font(back_label, bug_font_small, 0);
    lv_obj_center(back_label);

    spinner = lv_spinner_create(screen, 1000, 90);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);

    status_label = lv_label_create(screen);
    lv_obj_set_width(status_label, 280);
    lv_obj_set_style_text_font(status_label, bug_font_body, 0);
    lv_obj_set_style_text_color(status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 40);

    poll_timer = lv_timer_create(poll_cb, POLL_MS, NULL);

    bool returning_from_keyboard = from_keyboard;
    from_keyboard = false;
    if (connect_pending) {
        // Back from the keyboard with a password: connect now
        connect_pending = false;
        show_view(VIEW_CONNECTING);
        start_connect();
    } else if (returning_from_keyboard && scan_valid) {
        show_view(VIEW_LIST);
    } else {
        show_view(VIEW_SCANNING);
        start_scan();
    }
    ESP_LOGI(TAG, "Wi-Fi screen built");
    return screen;
}

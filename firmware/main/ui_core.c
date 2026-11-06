#include "ui_core.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_CORE";
static ui_context_t ui_ctx = {0};

// Littlevgl theme and styles
static lv_theme_t *theme_dark = NULL;
static lv_style_t style_bg_dark;
static lv_style_t style_text_primary;
static lv_style_t style_text_secondary;
static lv_style_t style_button;
static lv_style_t style_ring_green;
static lv_style_t style_ring_blue;
static lv_style_t style_ring_red;
static lv_style_t style_status_bar;

// Forward declarations
static void ui_init_styles(void);
static void ui_init_fonts(void);
static void ui_clip_circular_display(void);
static void ui_create_status_bar(void);

void ui_init(void) {
    ESP_LOGI(TAG, "Initializing UI (466x466 circular AMOLED)");

    // Initialize context
    ui_ctx.current_screen = SCREEN_HOME;
    ui_ctx.voice_state = VOICE_STATE_IDLE;
    ui_ctx.battery_percent = 100;
    ui_ctx.current_hour = 12;
    ui_ctx.current_minute = 0;
    ui_ctx.mic_muted = false;
    ui_ctx.wifi_connected = false;

    // Initialize fonts
    ui_init_fonts();

    // Create dark theme
    theme_dark = lv_theme_dark_init(COLOR_BG_DARK, COLOR_TEXT_PRIMARY);
    lv_theme_set_current(theme_dark);

    // Initialize styles
    ui_init_styles();

    // Get root screen
    ui_ctx.root_screen = lv_scr_act();
    lv_obj_set_size(ui_ctx.root_screen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_add_style(ui_ctx.root_screen, &style_bg_dark, 0);

    // Clip to circular display
    ui_clip_circular_display();

    // Create status bar (always visible)
    ui_create_status_bar();

    // Create home screen
    ui_create_home_screen();

    ESP_LOGI(TAG, "UI initialization complete");
}

void ui_deinit(void) {
    // Clean up theme and styles
    if (theme_dark) {
        lv_theme_delete(theme_dark);
        theme_dark = NULL;
    }
    ESP_LOGI(TAG, "UI deinitialized");
}

static void ui_init_fonts(void) {
    // Use default system fonts for now
    // In production: load custom fonts from SPIFFS/MicroSD
    font_large = &lv_font_montserrat_28;
    font_heading = &lv_font_montserrat_20;
    font_body = &lv_font_montserrat_16;
    font_small = &lv_font_montserrat_12;
    ESP_LOGI(TAG, "Fonts initialized");
}

static void ui_init_styles(void) {
    // Background style
    lv_style_init(&style_bg_dark);
    lv_style_set_bg_color(&style_bg_dark, COLOR_BG_DARK);
    lv_style_set_bg_opa(&style_bg_dark, LV_OPA_COVER);

    // Primary text style
    lv_style_init(&style_text_primary);
    lv_style_set_text_color(&style_text_primary, COLOR_TEXT_PRIMARY);
    lv_style_set_text_font(&style_text_primary, font_body);
    lv_style_set_text_line_space(&style_text_primary, 2);

    // Secondary text style
    lv_style_init(&style_text_secondary);
    lv_style_set_text_color(&style_text_secondary, COLOR_TEXT_SECONDARY);
    lv_style_set_text_font(&style_text_secondary, font_small);

    // Button style (minimalist)
    lv_style_init(&style_button);
    lv_style_set_bg_color(&style_button, COLOR_BG_DARK);
    lv_style_set_bg_opa(&style_button, LV_OPA_0);  // Transparent background
    lv_style_set_border_color(&style_button, COLOR_TEXT_PRIMARY);
    lv_style_set_border_width(&style_button, 1);
    lv_style_set_border_opa(&style_button, LV_OPA_30);
    lv_style_set_pad_all(&style_button, 8);
    lv_style_set_text_color(&style_button, COLOR_TEXT_PRIMARY);
    lv_style_set_text_font(&style_button, font_body);

    // Ring styles (for voice state indicators)
    lv_style_init(&style_ring_green);
    lv_style_set_bg_color(&style_ring_green, COLOR_ACCENT_GREEN);
    lv_style_set_bg_opa(&style_ring_green, LV_OPA_COVER);
    lv_style_set_border_opa(&style_ring_green, LV_OPA_0);

    lv_style_init(&style_ring_blue);
    lv_style_set_bg_color(&style_ring_blue, COLOR_ACCENT_BLUE);
    lv_style_set_bg_opa(&style_ring_blue, LV_OPA_COVER);
    lv_style_set_border_opa(&style_ring_blue, LV_OPA_0);

    lv_style_init(&style_ring_red);
    lv_style_set_bg_color(&style_ring_red, COLOR_ACCENT_RED);
    lv_style_set_bg_opa(&style_ring_red, LV_OPA_COVER);
    lv_style_set_border_opa(&style_ring_red, LV_OPA_0);

    // Status bar style
    lv_style_init(&style_status_bar);
    lv_style_set_bg_color(&style_status_bar, COLOR_BG_DARK);
    lv_style_set_bg_opa(&style_status_bar, LV_OPA_COVER);
    lv_style_set_border_color(&style_status_bar, COLOR_TEXT_SECONDARY);
    lv_style_set_border_width(&style_status_bar, 1);
    lv_style_set_border_side(&style_status_bar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_all(&style_status_bar, 4);

    ESP_LOGI(TAG, "Styles initialized");
}

static void ui_clip_circular_display(void) {
    // Create circular mask for 466x466 display
    // All objects outside the circle will be clipped

    // For simplicity, we'll manually clip in object creation
    // Set maximum width and height and rely on overflow hidden
    lv_obj_set_overflow(ui_ctx.root_screen, LV_OVERFLOW_HIDDEN);

    // Note: LVG doesn't have native circular clipping
    // We'll handle this by:
    // 1. Keeping all critical content within SAFE_AREA_RADIUS (190px)
    // 2. Status bar at top (small clip region)
    // 3. Navigation at bottom (small clip region)
    // 4. Main content centered (uses full 380px width max)

    ESP_LOGI(TAG, "Circular display clipping enabled");
}

static void ui_create_status_bar(void) {
    // Status bar: time | signal | battery | mute
    // Height: 30px, positioned at top center

    ui_ctx.status_bar = lv_obj_create(ui_ctx.root_screen);
    lv_obj_set_size(ui_ctx.status_bar, 400, 30);
    lv_obj_set_pos(ui_ctx.status_bar, 33, 5);  // Centered horizontally
    lv_obj_add_style(ui_ctx.status_bar, &style_status_bar, 0);

    // Time label (left)
    lv_obj_t *time_label = lv_label_create(ui_ctx.status_bar);
    lv_label_set_text(time_label, "3:45");
    lv_obj_set_pos(time_label, 5, 5);
    lv_obj_add_style(time_label, &style_text_primary, 0);
    lv_obj_set_style_text_font(time_label, font_heading, 0);

    // Battery label (right)
    lv_obj_t *battery_label = lv_label_create(ui_ctx.status_bar);
    lv_label_set_text(battery_label, "100%");
    lv_obj_set_pos(battery_label, 340, 5);
    lv_obj_add_style(battery_label, &style_text_secondary, 0);

    // WiFi indicator (if needed)
    // Mute indicator (if needed)

    ESP_LOGI(TAG, "Status bar created");
}

void ui_set_screen(screen_state_t screen) {
    if (ui_ctx.current_screen == screen) {
        return;  // Already on this screen
    }

    ESP_LOGI(TAG, "Switching to screen: %d", screen);

    // Clear main content area (keep status bar)
    // Iterate through children and remove non-status-bar objects
    lv_obj_t *child = lv_obj_get_child(ui_ctx.root_screen, NULL);
    while (child != NULL) {
        lv_obj_t *next = lv_obj_get_child(ui_ctx.root_screen, child);
        if (child != ui_ctx.status_bar && child != ui_ctx.bottom_nav) {
            lv_obj_del(child);
        }
        child = next;
    }

    ui_ctx.current_screen = screen;

    // Create screen-specific content
    switch (screen) {
        case SCREEN_HOME:
            ui_create_home_screen();
            break;
        case SCREEN_EMAIL_LIST:
            ui_create_email_list_screen();
            break;
        case SCREEN_CALENDAR:
            ui_create_calendar_screen();
            break;
        case SCREEN_SETTINGS:
            ui_create_settings_screen();
            break;
        case SCREEN_VOICE_FEEDBACK:
            // Handled by ui_show_voice_feedback overlay
            break;
        default:
            ESP_LOGW(TAG, "Unknown screen: %d", screen);
            break;
    }
}

void ui_set_voice_state(voice_state_t state) {
    ui_ctx.voice_state = state;
    ESP_LOGI(TAG, "Voice state changed: %d", state);

    // Update ring color based on state
    if (ui_ctx.voice_ring) {
        switch (state) {
            case VOICE_STATE_IDLE:
                lv_obj_set_style_bg_color(ui_ctx.voice_ring, COLOR_ACCENT_GRAY, 0);
                lv_obj_set_style_bg_opa(ui_ctx.voice_ring, LV_OPA_30, 0);
                break;
            case VOICE_STATE_LISTENING:
            case VOICE_STATE_STT:
                lv_obj_set_style_bg_color(ui_ctx.voice_ring, COLOR_ACCENT_GREEN, 0);
                lv_obj_set_style_bg_opa(ui_ctx.voice_ring, LV_OPA_COVER, 0);
                break;
            case VOICE_STATE_LLM:
                lv_obj_set_style_bg_color(ui_ctx.voice_ring, COLOR_ACCENT_BLUE, 0);
                lv_obj_set_style_bg_opa(ui_ctx.voice_ring, LV_OPA_COVER, 0);
                break;
            case VOICE_STATE_TTS:
                lv_obj_set_style_bg_color(ui_ctx.voice_ring, COLOR_ACCENT_WHITE, 0);
                lv_obj_set_style_bg_opa(ui_ctx.voice_ring, LV_OPA_COVER, 0);
                break;
            case VOICE_STATE_ERROR:
                lv_obj_set_style_bg_color(ui_ctx.voice_ring, COLOR_ACCENT_RED, 0);
                lv_obj_set_style_bg_opa(ui_ctx.voice_ring, LV_OPA_COVER, 0);
                break;
        }
    }
}

void ui_update_battery(uint8_t percent) {
    ui_ctx.battery_percent = percent;
    // Update status bar battery display
    // Color: green >50%, yellow 25-50%, red <25%
}

void ui_update_time(uint16_t hour, uint16_t minute) {
    ui_ctx.current_hour = hour;
    ui_ctx.current_minute = minute;
    // Update status bar time display
}

void ui_update_wifi_status(bool connected) {
    ui_ctx.wifi_connected = connected;
    // Update status bar WiFi indicator
}

void ui_mute_mic(bool muted) {
    ui_ctx.mic_muted = muted;
    // Update status bar mute indicator and ring appearance
}

void ui_show_voice_feedback(voice_state_t state, const char *text) {
    ui_set_voice_state(state);

    if (ui_ctx.center_label && text) {
        lv_label_set_text(ui_ctx.center_label, text);
    }
}

// Stub implementations for screen creation (detailed in next files)
lv_obj_t *ui_create_home_screen(void) {
    ESP_LOGI(TAG, "Creating home screen");
    return ui_ctx.root_screen;
}

lv_obj_t *ui_create_email_list_screen(void) {
    ESP_LOGI(TAG, "Creating email list screen");
    return ui_ctx.root_screen;
}

lv_obj_t *ui_create_calendar_screen(void) {
    ESP_LOGI(TAG, "Creating calendar screen");
    return ui_ctx.root_screen;
}

lv_obj_t *ui_create_settings_screen(void) {
    ESP_LOGI(TAG, "Creating settings screen");
    return ui_ctx.root_screen;
}

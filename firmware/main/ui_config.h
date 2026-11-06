#ifndef UI_CONFIG_H
#define UI_CONFIG_H

#include "lvgl.h"

// Display specifications
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_RADIUS 233
#define SAFE_AREA_RADIUS 190

// Color palette (dark theme, power-efficient)
#define COLOR_BG_DARK lv_color_make(18, 18, 18)           // #121212
#define COLOR_TEXT_PRIMARY lv_color_make(224, 224, 224)   // #E0E0E0
#define COLOR_TEXT_SECONDARY lv_color_make(153, 153, 153) // #999999
#define COLOR_ACCENT_GREEN lv_color_make(29, 185, 84)     // #1DB954 (listening)
#define COLOR_ACCENT_BLUE lv_color_make(30, 144, 255)     // #1E90FF (processing)
#define COLOR_ACCENT_WHITE lv_color_make(255, 255, 255)   // #FFFFFF (speaking)
#define COLOR_ACCENT_RED lv_color_make(255, 107, 107)     // #FF6B6B (error)
#define COLOR_ACCENT_GRAY lv_color_make(68, 68, 68)       // #444444 (idle)

// Typography sizes
#define FONT_SIZE_LARGE 28
#define FONT_SIZE_HEADING 20
#define FONT_SIZE_BODY 16
#define FONT_SIZE_SMALL 12

// Touch target minimum (44x44px per WCAG)
#define TOUCH_TARGET_MIN 44

// Animation timings (ms)
#define ANIM_TRANSITION_DEFAULT 200
#define ANIM_RING_PULSE_PERIOD 2000
#define ANIM_RING_SPIN_PERIOD 1500
#define ANIM_WAVEFORM_REFRESH 50

// Fonts (will be configured at runtime)
extern const lv_font_t *font_large;   // 28px
extern const lv_font_t *font_heading; // 20px
extern const lv_font_t *font_body;    // 16px
extern const lv_font_t *font_small;   // 12px

// UI layer enumeration
typedef enum {
    UI_LAYER_HOME = 0,
    UI_LAYER_EMAIL = 1,
    UI_LAYER_CALENDAR = 2,
    UI_LAYER_SETTINGS = 3,
    UI_LAYER_JOKE = 4,
    UI_LAYER_RADIO = 5,
    UI_LAYER_CRYPTO = 6,
    UI_LAYER_BOOK = 7,
    UI_LAYER_STORY = 8,
    UI_LAYER_VOICE_FEEDBACK = 10  // Overlay
} ui_layer_t;

// Screen state machine
typedef enum {
    SCREEN_HOME,
    SCREEN_VOICE_FEEDBACK,
    SCREEN_EMAIL_LIST,
    SCREEN_EMAIL_DETAIL,
    SCREEN_CALENDAR,
    SCREEN_SETTINGS,
    SCREEN_POWER_PROFILE,
    SCREEN_ERROR,
    SCREEN_BATTERY_LOW
} screen_state_t;

// Voice feedback state (displayed as animated ring)
typedef enum {
    VOICE_STATE_IDLE,      // Gray ring, dim
    VOICE_STATE_LISTENING, // Green ring, pulsing
    VOICE_STATE_STT,       // Green ring, steady + waveform
    VOICE_STATE_LLM,       // Blue ring, rotating spinner
    VOICE_STATE_TTS,       // White ring, glowing + text scroll
    VOICE_STATE_ERROR      // Red ring, steady
} voice_state_t;

// UI context structure
typedef struct {
    screen_state_t current_screen;
    voice_state_t voice_state;
    lv_obj_t *root_screen;
    lv_obj_t *status_bar;
    lv_obj_t *voice_ring;
    lv_obj_t *center_label;
    lv_obj_t *bottom_nav;
    uint8_t battery_percent;
    uint16_t current_hour;
    uint16_t current_minute;
    bool mic_muted;
    bool wifi_connected;
} ui_context_t;

// Function declarations
void ui_init(void);
void ui_deinit(void);
void ui_set_screen(screen_state_t screen);
void ui_set_voice_state(voice_state_t state);
void ui_update_battery(uint8_t percent);
void ui_update_time(uint16_t hour, uint16_t minute);
void ui_update_wifi_status(bool connected);
void ui_mute_mic(bool muted);
lv_obj_t *ui_create_home_screen(void);
lv_obj_t *ui_create_email_list_screen(void);
lv_obj_t *ui_create_calendar_screen(void);
lv_obj_t *ui_create_settings_screen(void);
void ui_show_voice_feedback(voice_state_t state, const char *text);

#endif // UI_CONFIG_H

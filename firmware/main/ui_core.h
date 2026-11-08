#ifndef UI_CORE_H
#define UI_CORE_H

#include "ui_config.h"

// Core UI lifecycle
void ui_init(void);
void ui_deinit(void);

// Screen management
void ui_set_screen(screen_state_t screen);

// Voice feedback feedback (updates ring color + text)
void ui_set_voice_state(voice_state_t state);
void ui_show_voice_feedback(voice_state_t state, const char *text);

// Status bar updates
void ui_update_battery(uint8_t percent);
void ui_update_power(int percent, bool charging);  // percent < 0 when there's no battery
void ui_update_time(uint16_t hour, uint16_t minute);  // hour > 23 shows "--:--"
void ui_update_wifi_status(bool connected);
void ui_mute_mic(bool muted);

// A small message above the battery that disappears by itself after `ms` ("Batería baja").
void ui_notice(const char *text, lv_color_t color, uint32_t ms);

// Replaces the clock with a short colored message ("Te escucho"); NULL brings the clock back.
void ui_voice_banner(const char *text, lv_color_t color);

#endif // UI_CORE_H

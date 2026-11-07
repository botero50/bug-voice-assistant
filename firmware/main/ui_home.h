#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"

// Home screen creation
lv_obj_t *ui_create_home_screen(void);

// Replaces the text under the ring
void ui_home_set_status(const char *text);

// Colors the ring around the microphone icon
void ui_home_set_ring_color(lv_color_t color);

// Animation helpers
void ui_animate_ring_pulse(lv_obj_t *ring);
void ui_animate_ring_spin(lv_obj_t *ring);
void ui_stop_ring_animation(void);

#endif // UI_HOME_H

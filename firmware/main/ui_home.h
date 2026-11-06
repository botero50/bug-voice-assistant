#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"

// Home screen creation
lv_obj_t *ui_create_home_screen(void);

// Animation helpers
void ui_animate_ring_pulse(lv_obj_t *ring);
void ui_animate_ring_spin(lv_obj_t *ring);
void ui_stop_ring_animation(void);

#endif // UI_HOME_H

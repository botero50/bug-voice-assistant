#ifndef UI_ALARM_H
#define UI_ALARM_H

#include "lvgl.h"

// The screen that comes up when a timer, alarm or reminder rings: a red pulsing ring with a bell,
// what it is for, and a Stop button. Tapping the ring also stops it.

void ui_alarm_set_label(const char *label);

lv_obj_t *ui_alarm_build(void);

#endif // UI_ALARM_H

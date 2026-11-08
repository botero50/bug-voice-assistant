#ifndef UI_STOPWATCH_H
#define UI_STOPWATCH_H

#include "lvgl.h"

// The stopwatch screen: opened by voice ("inicia el cronómetro"). Tap the buttons to start,
// pause or reset it. Swipe up goes Home; the stopwatch keeps counting.
lv_obj_t *ui_stopwatch_build(void);

#endif

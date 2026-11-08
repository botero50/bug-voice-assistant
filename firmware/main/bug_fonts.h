#ifndef BUG_FONTS_H
#define BUG_FONTS_H

#include "esp_err.h"
#include "lvgl.h"

// Montserrat with Spanish letters. If the font can't be created these point at
// LVGL's built-in fonts, which draw plain text but no accents.
extern const lv_font_t *bug_font_small;  // transcript and small labels
extern const lv_font_t *bug_font_body;   // answers
extern const lv_font_t *bug_font_time;   // clock
extern const lv_font_t *bug_font_huge;   // the temperature

// Call once after lv_init().
esp_err_t bug_fonts_init(void);

#endif

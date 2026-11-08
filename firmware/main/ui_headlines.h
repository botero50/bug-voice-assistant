#ifndef UI_HEADLINES_H
#define UI_HEADLINES_H

#include <stdbool.h>
#include "lvgl.h"

// The news screen. It draws what the server sends (JSON: title, items, footer), opened by voice:
// "dame las noticias".

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_headlines_set_data(const char *json);

// Up to 4 headlines, each up to two lines with its source underneath.
lv_obj_t *ui_headlines_build(void);

#endif // UI_HEADLINES_H

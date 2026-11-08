#ifndef UI_ARTICLE_H
#define UI_ARTICLE_H

#include <stdbool.h>
#include "lvgl.h"

// A title and a block of text: a Wikipedia answer ("¿quién fue Simón Bolívar?") or a joke
// ("cuéntame un chiste"). It draws what the server sends (JSON: title, text, footer).

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_article_set_data(const char *json);

lv_obj_t *ui_article_build(void);

#endif // UI_ARTICLE_H

#ifndef UI_ROWS_H
#define UI_ROWS_H

#include <stdbool.h>
#include "lvgl.h"

// A screen of labelled values: crypto prices, the dollar and euro, this PC's status.
// It draws what the server sends (JSON: title, rows, footer), opened by voice:
// "¿cuánto está el bitcoin?", "¿a cuánto está el dólar?", "¿cómo está mi computador?".

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_rows_set_data(const char *json);

// A title, up to 4 rows (name and note on the left, value and change on the right) and a footer.
lv_obj_t *ui_rows_build(void);

#endif // UI_ROWS_H

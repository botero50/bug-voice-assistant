#ifndef UI_CALENDAR_H
#define UI_CALENDAR_H

#include <stdbool.h>
#include "lvgl.h"

// The agenda screen. It draws what the server sends (JSON), so the same screen will show
// real events once the calendars are connected. Opened by voice: "¿qué tengo en el calendario?".

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_calendar_set_data(const char *json);

// Today and tomorrow, each with its events: time, title and place.
lv_obj_t *ui_calendar_build(void);

#endif // UI_CALENDAR_H

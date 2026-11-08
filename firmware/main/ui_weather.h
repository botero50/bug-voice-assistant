#ifndef UI_WEATHER_H
#define UI_WEATHER_H

#include <stdbool.h>
#include "lvgl.h"

// The weather screen. It draws what the server sends (JSON), opened by voice:
// "¿cómo está el clima?" or "¿va a llover en Medellín?".

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_weather_set_data(const char *json);

// City, a drawn icon, the temperature, today's range and rain chance, and the next 3 days.
lv_obj_t *ui_weather_build(void);

#endif // UI_WEATHER_H

#ifndef UI_MUSIC_H
#define UI_MUSIC_H

#include <stdbool.h>
#include "lvgl.h"

// The radio screen. It draws what the server sends (JSON: station and detail), opened by voice:
// "pon salsa", "pon Tropicana", "pon música". The equalizer follows the real audio level.

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_music_set_data(const char *json);

// Station name, a ring that moves with the music, "tap to talk" and a Stop button.
lv_obj_t *ui_music_build(void);

#endif // UI_MUSIC_H

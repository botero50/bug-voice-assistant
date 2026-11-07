#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"

typedef enum {
    UI_HOME_STANDBY,    // Gray ring breathing slowly
    UI_HOME_HEARING,    // Green ring, waveform of what the mic hears
    UI_HOME_LISTENING,  // Same, after the wake word: waiting for the command
    UI_HOME_THINKING,   // Blue ring with a spinner
    UI_HOME_SPEAKING,   // White ring pulsing at 2 Hz
    UI_HOME_ERROR,      // Red ring
} ui_home_mode_t;

lv_obj_t *ui_create_home_screen(void);

void ui_home_set_mode(ui_home_mode_t mode);

// What the user said (small, gray) and what Bug answers (large). "" clears a line.
void ui_home_set_texts(const char *heard, const char *answer);

// Microphone level from 0 to 1, pushed regularly while the waveform is showing.
void ui_home_push_level(float level);

#endif // UI_HOME_H

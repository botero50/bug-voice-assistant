#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"

typedef enum {
    UI_HOME_STANDBY,    // Gray ring breathing slowly
    UI_HOME_HEARING,    // Green ring, waveform of what the mic hears
    UI_HOME_LISTENING,  // Same, after the wake word: waiting for the command
    UI_HOME_THINKING,   // Blue ring with a spinner
    UI_HOME_SPEAKING,   // White ring pulsing at 2 Hz
    UI_HOME_MUTED,      // Microphone off
    UI_HOME_MUSIC,      // Purple ring: the radio is playing, tap the ring to talk
    UI_HOME_SETUP,      // Amber: Wi-Fi needs attention, tapping the ring opens the list
    UI_HOME_ERROR,      // Red ring
} ui_home_mode_t;

// Builds the home screen (not yet shown). The ring reacts to taps: a tap listens
// right away, a long press mutes the microphone.
lv_obj_t *ui_home_build(void);

// These remember the latest values, so the screen is right when it is built again.
void ui_home_set_mode(ui_home_mode_t mode);

// What the user said (small, gray) and what Bug answers (large). "" clears a line.
void ui_home_set_texts(const char *heard, const char *answer);

// Microphone level from 0 to 1, pushed regularly while the waveform is showing.
void ui_home_push_level(float level);

// Touch on the middle of the screen: a tap listens, a long press mutes the microphone (also used by the Lumina look)
void ui_home_ring_event(lv_event_t *e);

// The Lumina Kinetic look (ui_home_lumina.c), the default. The cobot telemetry look is in ui_home.c.
lv_obj_t *ui_home_lumina_build(ui_home_mode_t start_mode, const char *heard, const char *answer);
void ui_home_lumina_set_mode(ui_home_mode_t mode);
void ui_home_lumina_set_texts(const char *heard, const char *answer);
void ui_home_lumina_push_level(float level);

#endif // UI_HOME_H

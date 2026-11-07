#ifndef UI_NAV_H
#define UI_NAV_H

#include <stdbool.h>
#include "lvgl.h"

typedef enum {
    UI_SCREEN_HOME,
    UI_SCREEN_EMAIL,
    UI_SCREEN_CALENDAR,
    UI_SCREEN_SKILLS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_HISTORY,
    UI_SCREEN_WIFI,
    UI_SCREEN_TEXT_ENTRY,
} ui_screen_id_t;

// Swipes (see UI_UX_DESIGN_PLAN.md):
//   Home / Email / Calendar / Skills: left and right cycle through them
//   Home: up opens Settings, down opens History; up on the others goes back Home
//   Settings, History: up goes back Home
//   WiFi list, keyboard: right goes back

// Builds the home screen and shows it.
void ui_nav_start(void);

void ui_nav_go(ui_screen_id_t id, lv_scr_load_anim_t anim);

// For screens built elsewhere (the keyboard): registers swipes and shows it.
void ui_nav_load_screen(lv_obj_t *screen, ui_screen_id_t id, lv_scr_load_anim_t anim);

ui_screen_id_t ui_nav_current(void);

// Goes back Home when the assistant starts listening, unless the user is typing.
void ui_nav_show_home_for_voice(void);

// False for a moment after a swipe, so lifting the finger isn't also a tap.
bool ui_nav_click_allowed(void);

#endif

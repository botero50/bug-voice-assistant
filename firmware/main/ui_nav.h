#ifndef UI_NAV_H
#define UI_NAV_H

#include <stdbool.h>
#include "lvgl.h"

typedef enum {
    UI_SCREEN_HOME,
    UI_SCREEN_EMAIL,         // opened by voice
    UI_SCREEN_EMAIL_DETAIL,  // opened by voice or by tapping an email
    UI_SCREEN_CALENDAR,      // opened by voice
    UI_SCREEN_WEATHER,       // opened by voice
    UI_SCREEN_MUSIC,         // opened by voice
    UI_SCREEN_ROWS,          // opened by voice: crypto, dollar, this PC's status
    UI_SCREEN_HEADLINES,     // opened by voice: the news
    UI_SCREEN_ARTICLE,       // opened by voice: Wikipedia answers and jokes
    UI_SCREEN_ALARM,         // opens by itself when a timer, alarm or reminder rings
    UI_SCREEN_SETTINGS,
    UI_SCREEN_HISTORY,
    UI_SCREEN_WIFI,
    UI_SCREEN_TEXT_ENTRY,
    UI_SCREEN_STOPWATCH,     // opened by voice: "inicia el cronómetro"
    UI_SCREEN_GAMES,         // the gaming area's menu: "juguemos"
    UI_SCREEN_GAME,          // the game being played
    UI_SCREEN_TRIQUI,        // opened by voice: "juguemos al triqui"
} ui_screen_id_t;

// Screens are opened by voice (the server names the screen and sends its data).
// Touch works inside a screen. Swipes (see UI_UX_DESIGN_PLAN.md):
//   Home: up opens Settings, down opens History
//   Every other screen: up goes back Home
//   Email detail, Wi-Fi list, keyboard: right goes back one step

// Builds the home screen and shows it.
void ui_nav_start(void);

void ui_nav_go(ui_screen_id_t id, lv_scr_load_anim_t anim);

// For screens built elsewhere (the keyboard): registers swipes and shows it.
void ui_nav_load_screen(lv_obj_t *screen, ui_screen_id_t id, lv_scr_load_anim_t anim);

ui_screen_id_t ui_nav_current(void);

// Opens what a voice answer asked for. path is "home", "email", "email_detail?i=0" or
// "calendar" or "weather?q=medellin"; json is the data for it (NULL for "home").
void ui_nav_open_screen(const char *path, const char *json);

// Shows the ringing screen over whatever is open.
void ui_nav_open_alarm(const char *label);

// Goes back Home so the answer can be shown, unless the user is typing.
void ui_nav_show_home_for_voice(void);

// Call about once a second: returns Home when a screen has been left alone for a minute.
void ui_nav_tick(void);

// Resets that minute, e.g. while the user is talking to Bug.
void ui_nav_keep_alive(void);

// Treats the touch happening right now as a gesture, not a tap (used when a touch wakes the screen).
void ui_nav_swallow_touch(void);

// False for a moment after a swipe, so lifting the finger isn't also a tap.
bool ui_nav_click_allowed(void);

#endif

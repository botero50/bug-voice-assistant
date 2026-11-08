#ifndef UI_GAMES_INTERNAL_H
#define UI_GAMES_INTERNAL_H

#include "bug_fonts.h"
#include "esp_random.h"
#include "lvgl.h"
#include "ui_config.h"
#include "ui_nav.h"
#include <stdbool.h>
#include <stdint.h>

// Shared by ui_games.c (the menu and the frame of every game) and the files with the games themselves
// (ui_games.c, games_action.c, games_mind.c). The order of the games is the order of the menu, by category.

typedef enum {
    // Fuerza
    G_MOLES, G_BALLOON, G_ROLL, G_MAZE,
    // Velocidad
    G_REACT, G_TAPS, G_DROP, G_ROPE, G_FLAPPY,
    // Defensa
    G_CATCH, G_BALANCE, G_BRICK, G_MEMORY, G_SIMON,
    // Mente
    G_SHAPE, G_QUIZ, G_SLIDE, G_HIGHLOW, G_RIDDLE, G_HANG, G_ANAGRAM,
    // Diversión
    G_LR, G_DANCE, G_HIDE, G_RUN, G_AKI,
    // Arcade
    G_MERGE, G_BUBBLES, G_DRAW,
    G_COUNT
} game_id_t;

// The play area: a square in the middle of the round screen. Salir sits under it.
#define ARENA_X 111
#define ARENA_Y 118
#define ARENA_W 244
#define ARENA_H 244

extern bool g_playing;      // the game is on (false after it ended)
extern lv_timer_t *g_timer; // the game's timer; it is removed when the screen goes away

lv_obj_t *g_frame(game_id_t g);              // the screen of a game: title, a line of text, Salir
lv_obj_t *g_arena(lv_obj_t *screen);         // the play area (transparent, takes touches)
lv_obj_t *g_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width);
lv_obj_t *g_button(lv_obj_t *parent, int w, int h, lv_color_t color, const char *text, const lv_font_t *font);
lv_obj_t *g_box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius);  // a rectangle (a circle if radius is big)
void g_say(const char *format, ...) __attribute__((format(printf, 1, 2)));  // the line of text under the title
void g_finish(game_id_t g, uint16_t result, const char *what);               // keeps a record, offers "Otra vez"
uint16_t g_best(game_id_t g);
void g_beep(float hz, int ms);
uint32_t g_rand(uint32_t n);                 // 0 .. n-1
lv_color_t g_hue(int i);                     // eight bright colours
void g_on_close(void (*fn)(void));           // called when the game's screen is deleted (free memory)
bool g_click_ok(void);
#define CIRCLE_RADIUS LV_RADIUS_CIRCLE                       // false right after a swipe

// Every game's builder
lv_obj_t *build_moles(void), *build_balloon(void), *build_roll(void), *build_maze(void);
lv_obj_t *build_react(void), *build_taps(void), *build_drop(void), *build_rope(void), *build_flappy(void);
lv_obj_t *build_catch(void), *build_balance(void), *build_brick(void), *build_memory(void), *build_simon(void);
lv_obj_t *build_shape(void), *build_quiz(void), *build_slide(void), *build_highlow(void);
lv_obj_t *build_lr(void), *build_dance(void), *build_hide(void), *build_run(void);
lv_obj_t *build_merge(void), *build_bubbles(void), *build_draw(void);
lv_obj_t *build_riddle(void), *build_hang(void), *build_anagram(void), *build_aki(void);

void g_offer_again(void);      // shows the "Otra vez" button (the game ended in a way g_finish does not cover)
bool g_screen_alive(void);     // the game's screen still exists (for timers that fire later)

#endif

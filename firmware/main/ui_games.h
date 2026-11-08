#ifndef UI_GAMES_H
#define UI_GAMES_H

#include "lvgl.h"
#include <stdbool.h>

// The gaming area: a menu of touch games (UI_SCREEN_GAMES) and the screen of the game being played (UI_SCREEN_GAME).
// The rules of "Merge" come from the TamaPoke project (MIT, see CREDITS.md); everything is drawn with plain shapes.

lv_obj_t *ui_games_hub_build(void);
lv_obj_t *ui_game_build(void);

// What the voice asks for: "" or "hub" opens the menu, otherwise the id of a game ("merge", "memory", "highlow", "moles", "simon")
// or "triqui". Returns the screen to open: 0 = the menu, 1 = a game, 2 = tic-tac-toe.
int ui_games_choose(const char *game);

// For measuring: the menu scrolls itself up and down (see "games?bench")
void ui_games_set_bench(bool on);

#endif

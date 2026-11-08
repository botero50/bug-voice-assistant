#ifndef UI_TRIQUI_H
#define UI_TRIQUI_H

#include "lvgl.h"

// Tic-tac-toe ("triqui") against the board: opened by voice. You are X and tap a square; Bug plays O.
// Bug sometimes misses a good move, so it can be beaten. After the game ends, tap the board for a new one.
// Swipe up goes Home.
lv_obj_t *ui_triqui_build(void);

#endif

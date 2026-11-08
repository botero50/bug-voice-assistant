#include "ui_triqui.h"
#include "bug_fonts.h"
#include "ui_config.h"
#include "ui_nav.h"
#include "esp_random.h"

#define CELL 80
#define GAP 8
#define GRID_TOP 92
#define MISTAKE_PERCENT 25

enum { EMPTY = 0, HUMAN = 1, BOARD = 2 };

static int cells[9];
static bool over;
static lv_obj_t *buttons[9];
static lv_obj_t *marks[9];
static lv_obj_t *status;

static const int LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

// Who has three in a row (HUMAN or BOARD), or EMPTY
static int winner(const int *b) {
    for (int i = 0; i < 8; i++) {
        int a = b[LINES[i][0]];
        if (a && a == b[LINES[i][1]] && a == b[LINES[i][2]]) {
            return a;
        }
    }
    return EMPTY;
}

static bool full(const int *b) {
    for (int i = 0; i < 9; i++) {
        if (!b[i]) return false;
    }
    return true;
}

// Minimax: positive is good for the board
static int score(int *b, int turn, int depth) {
    int w = winner(b);
    if (w == BOARD) return 10 - depth;
    if (w == HUMAN) return depth - 10;
    if (full(b)) return 0;
    int best = turn == BOARD ? -100 : 100;
    for (int i = 0; i < 9; i++) {
        if (b[i]) continue;
        b[i] = turn;
        int s = score(b, turn == BOARD ? HUMAN : BOARD, depth + 1);
        b[i] = EMPTY;
        if (turn == BOARD ? s > best : s < best) best = s;
    }
    return best;
}

static int pick_move(void) {
    int free_cells[9], count = 0;
    for (int i = 0; i < 9; i++) {
        if (!cells[i]) free_cells[count++] = i;
    }
    if ((int)(esp_random() % 100) < MISTAKE_PERCENT) {
        return free_cells[esp_random() % count];  // a slip, so the player can win
    }
    int best_cell = free_cells[0], best_score = -100;
    for (int k = 0; k < count; k++) {
        int i = free_cells[k];
        cells[i] = BOARD;
        int s = score(cells, HUMAN, 1);
        cells[i] = EMPTY;
        if (s > best_score) {
            best_score = s;
            best_cell = i;
        }
    }
    return best_cell;
}

static void draw(void) {
    for (int i = 0; i < 9; i++) {
        lv_label_set_text(marks[i], cells[i] == HUMAN ? "X" : cells[i] == BOARD ? "O" : "");
        lv_obj_set_style_text_color(marks[i], cells[i] == HUMAN ? COLOR_ACCENT_GREEN : COLOR_ACCENT_BLUE, 0);
        lv_obj_center(marks[i]);
    }
}

static void finish_if_over(void) {
    int w = winner(cells);
    if (w == HUMAN) {
        lv_label_set_text(status, "¡Ganaste! Toca para jugar otra vez");
        over = true;
    } else if (w == BOARD) {
        lv_label_set_text(status, "Gané yo. Toca para jugar otra vez");
        over = true;
    } else if (full(cells)) {
        lv_label_set_text(status, "Empate. Toca para jugar otra vez");
        over = true;
    }
}

static void new_game(void) {
    for (int i = 0; i < 9; i++) cells[i] = EMPTY;
    over = false;
    lv_label_set_text(status, "Tu turno: eres la X");
    draw();
}

static void cell_cb(lv_event_t *e) {
    if (!ui_nav_click_allowed()) {
        return;
    }
    if (over) {
        new_game();
        return;
    }
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (cells[i]) {
        return;
    }
    cells[i] = HUMAN;
    finish_if_over();
    if (!over) {
        cells[pick_move()] = BOARD;
        finish_if_over();
    }
    draw();
}

lv_obj_t *ui_triqui_build(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    for (int i = 0; i < 9; i++) {
        int row = i / 3, col = i % 3;
        lv_obj_t *button = lv_btn_create(screen);
        lv_obj_set_size(button, CELL, CELL);
        lv_obj_align(button, LV_ALIGN_TOP_MID, (col - 1) * (CELL + GAP), GRID_TOP + row * (CELL + GAP));
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_bg_color(button, lv_color_make(38, 38, 38), 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, cell_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        buttons[i] = button;

        marks[i] = lv_label_create(button);
        lv_obj_set_style_text_font(marks[i], bug_font_huge, 0);
        lv_label_set_text(marks[i], "");
        lv_obj_center(marks[i]);
    }

    status = lv_label_create(screen);
    lv_obj_set_width(status, 300);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(status, bug_font_small, 0);
    lv_obj_set_style_text_color(status, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, GRID_TOP + 3 * (CELL + GAP) + 6);

    new_game();
    return screen;
}

#include "esp_heap_caps.h"
#include "imu.h"
#include "ui_games_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// The thinking and quick-reaction games of the gaming area: Izquierda o derecha, Escondite (cups), Laberinto, Puzle (slide),
// Burbujas, Dibujo, Figuras, Preguntas, Reacción and Toques. Played by touch, drawn with plain shapes.

#define CIRCLE LV_RADIUS_CIRCLE

static lv_obj_t *arena;

static void make_arena(lv_obj_t *screen) {
    arena = g_arena(screen);
}

static void show(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void hide(lv_obj_t *o) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_x_cb(void *obj, int32_t v) {
    lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)v);
}

// ================================================================================================ Izquierda o derecha
static int lr_lives, lr_score;
static lv_obj_t *lr_circle, *lr_symbol;

static void lr_guess(lv_event_t *e) {
    if (!g_playing) return;
    int guess = (int)(intptr_t)lv_event_get_user_data(e);  // 0 left, 1 right
    int answer = (int)g_rand(2);
    bool right = guess == answer;
    if (right) lr_score++;
    else lr_lives--;
    lv_label_set_text(lr_symbol, answer ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
    lv_obj_set_style_bg_color(lr_circle, right ? g_hue(2) : g_hue(0), 0);
    if (lr_lives <= 0) {
        g_finish(G_LR, (uint16_t)lr_score, "Fin · puntos");
        return;
    }
    g_say("%s · puntos %d · vidas %d", right ? "¡Acertaste!" : "Fallaste", lr_score, lr_lives);
}

lv_obj_t *build_lr(void) {
    lv_obj_t *screen = g_frame(G_LR);
    make_arena(screen);
    lr_lives = 3;
    lr_score = 0;
    lr_circle = g_box(arena, ARENA_W / 2 - 55, 14, 110, 110, lv_color_make(60, 64, 80), CIRCLE);
    lr_symbol = lv_label_create(lr_circle);
    lv_label_set_text(lr_symbol, "?");
    lv_obj_set_style_text_font(lr_symbol, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lr_symbol, lv_color_white(), 0);
    lv_obj_center(lr_symbol);
    lv_obj_t *left = g_button(arena, 112, 56, g_hue(4), LV_SYMBOL_LEFT "  Izq.", bug_font_small);
    lv_obj_set_pos(left, 4, 150);
    lv_obj_add_event_cb(left, lr_guess, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)0);
    lv_obj_t *right = g_button(arena, 112, 56, g_hue(5), "Der.  " LV_SYMBOL_RIGHT, bug_font_small);
    lv_obj_set_pos(right, 128, 150);
    lv_obj_add_event_cb(right, lr_guess, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)1);
    g_say("¿Hacia dónde gira? Vidas 3");
    return screen;
}

// ======================================================================================================== Escondite
#define CUPS 3
static lv_obj_t *cup[CUPS], *cup_ball;
static int slot_of[CUPS], ball_cup, swaps_left, hide_round, hide_score, busy_ms, hide_stage, stage_ms;
enum { H_SHOW, H_SWAP, H_GUESS, H_REVEAL };

static int slot_x(int slot) {
    return 14 + slot * 80;
}

static void move_cup(int c, int dur) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, cup[c]);
    lv_anim_set_values(&a, lv_obj_get_x(cup[c]), slot_x(slot_of[c]));
    lv_anim_set_time(&a, (uint32_t)dur);
    lv_anim_set_exec_cb(&a, set_x_cb);
    lv_anim_start(&a);
}

static void hide_round_start(void) {
    ball_cup = (int)g_rand(CUPS);
    swaps_left = 3 + hide_round;
    if (swaps_left > 11) swaps_left = 11;
    for (int c = 0; c < CUPS; c++) {
        slot_of[c] = c;
        lv_obj_set_pos(cup[c], slot_x(c), 30);  // lifted: the ball is seen
    }
    lv_obj_set_x(cup_ball, slot_x(ball_cup) + 15);
    show(cup_ball);
    hide_stage = H_SHOW;
    stage_ms = 1100;
    g_say("Mira dónde está la bola · ronda %d", hide_round + 1);
}

static void hide_tick(lv_timer_t *t) {
    if (!g_playing) return;
    if (hide_stage == H_SHOW) {
        if ((stage_ms -= 100) <= 0) {
            for (int c = 0; c < CUPS; c++) lv_obj_set_y(cup[c], 70);
            hide_stage = H_SWAP;
            busy_ms = 300;
        }
    } else if (hide_stage == H_SWAP) {
        if ((busy_ms -= 100) > 0) return;
        if (swaps_left <= 0) {
            hide_stage = H_GUESS;
            g_say("¿Dónde está la bola?");
            return;
        }
        int a = (int)g_rand(CUPS), b;
        do {
            b = (int)g_rand(CUPS);
        } while (b == a);
        int tmp = slot_of[a];
        slot_of[a] = slot_of[b];
        slot_of[b] = tmp;
        int dur = 380 - hide_round * 28;
        if (dur < 150) dur = 150;
        move_cup(a, dur);
        move_cup(b, dur);
        busy_ms = dur + 60;
        swaps_left--;
    } else if (hide_stage == H_REVEAL) {
        if ((stage_ms -= 100) <= 0) {
            hide_round++;
            hide_round_start();
        }
    }
}

static void cup_tap(lv_event_t *e) {
    int c = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing || hide_stage != H_GUESS) return;
    lv_obj_set_x(cup_ball, slot_x(slot_of[ball_cup]) + 15);
    lv_obj_set_y(cup[ball_cup], 30);
    show(cup_ball);
    if (c == ball_cup) {
        hide_score++;
        hide_stage = H_REVEAL;
        stage_ms = 900;
        g_say("¡Bien! Rondas %d", hide_score);
    } else {
        lv_obj_set_y(cup[c], 30);
        g_finish(G_HIDE, (uint16_t)hide_score, "No era ahí · rondas");
    }
}

lv_obj_t *build_hide(void) {
    lv_obj_t *screen = g_frame(G_HIDE);
    make_arena(screen);
    hide_round = 0;
    hide_score = 0;
    cup_ball = g_box(arena, 0, 112, 30, 30, g_hue(1), CIRCLE);
    for (int c = 0; c < CUPS; c++) {
        cup[c] = g_box(arena, slot_x(c), 70, 60, 80, g_hue(c == 0 ? 4 : c == 1 ? 5 : 3), 14);
        lv_obj_add_flag(cup[c], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cup[c], cup_tap, LV_EVENT_PRESSED, (void *)(intptr_t)c);
    }
    hide_round_start();
    g_timer = lv_timer_create(hide_tick, 100, NULL);
    return screen;
}

// ======================================================================================================= Laberinto
#define MAZE_N 7
#define MAZE_CELL 32
#define N_WALL 1
#define E_WALL 2
#define S_WALL 4
#define W_WALL 8
static uint8_t walls[MAZE_N * MAZE_N];
static lv_obj_t *maze_player;
static int px, py, maze_moves;

static void maze_generate(void) {
    memset(walls, N_WALL | E_WALL | S_WALL | W_WALL, sizeof(walls));
    bool seen[MAZE_N * MAZE_N] = {false};
    int stack[MAZE_N * MAZE_N], top = 0;
    stack[top++] = 0;
    seen[0] = true;
    while (top > 0) {
        int cell = stack[top - 1], x = cell % MAZE_N, y = cell / MAZE_N;
        int options[4], count = 0;
        if (y > 0 && !seen[cell - MAZE_N]) options[count++] = 0;
        if (x < MAZE_N - 1 && !seen[cell + 1]) options[count++] = 1;
        if (y < MAZE_N - 1 && !seen[cell + MAZE_N]) options[count++] = 2;
        if (x > 0 && !seen[cell - 1]) options[count++] = 3;
        if (!count) {
            top--;
            continue;
        }
        int dir = options[g_rand((uint32_t)count)], next;
        if (dir == 0) { next = cell - MAZE_N; walls[cell] &= (uint8_t)~N_WALL; walls[next] &= (uint8_t)~S_WALL; }
        else if (dir == 1) { next = cell + 1; walls[cell] &= (uint8_t)~E_WALL; walls[next] &= (uint8_t)~W_WALL; }
        else if (dir == 2) { next = cell + MAZE_N; walls[cell] &= (uint8_t)~S_WALL; walls[next] &= (uint8_t)~N_WALL; }
        else { next = cell - 1; walls[cell] &= (uint8_t)~W_WALL; walls[next] &= (uint8_t)~E_WALL; }
        seen[next] = true;
        stack[top++] = next;
    }
}

static void maze_place(void) {
    int off = (ARENA_W - MAZE_N * MAZE_CELL) / 2;
    lv_obj_set_pos(maze_player, off + px * MAZE_CELL + 7, off + py * MAZE_CELL + 7);
}

static void maze_move(lv_dir_t d) {
    int cell = py * MAZE_N + px, nx = px, ny = py;
    if (d == LV_DIR_TOP && !(walls[cell] & N_WALL)) ny--;
    else if (d == LV_DIR_BOTTOM && !(walls[cell] & S_WALL)) ny++;
    else if (d == LV_DIR_LEFT && !(walls[cell] & W_WALL)) nx--;
    else if (d == LV_DIR_RIGHT && !(walls[cell] & E_WALL)) nx++;
    else return;
    px = nx;
    py = ny;
    maze_moves++;
    maze_place();
    g_say("Movimientos %d", maze_moves);
    if (px == MAZE_N - 1 && py == MAZE_N - 1) g_finish(G_MAZE, (uint16_t)maze_moves, "¡Llegaste! movimientos");
}

static void maze_gesture(lv_event_t *e) {
    if (g_playing) maze_move(lv_indev_get_gesture_dir(lv_indev_get_act()));
}

static uint32_t maze_tilt_at;

static void maze_tilt_tick(lv_timer_t *t) {
    float right, down;
    if (!g_playing || !imu_tilt(&right, &down) || lv_tick_get() < maze_tilt_at) return;
    float strongest = fabsf(right) > fabsf(down) ? fabsf(right) : fabsf(down);
    if (strongest < 0.3f) return;
    maze_move(fabsf(right) > fabsf(down) ? (right > 0 ? LV_DIR_RIGHT : LV_DIR_LEFT) : (down > 0 ? LV_DIR_BOTTOM : LV_DIR_TOP));
    maze_tilt_at = lv_tick_get() + 320;
}

lv_obj_t *build_maze(void) {
    lv_obj_t *screen = g_frame(G_MAZE);
    make_arena(screen);
    maze_generate();
    px = py = 0;
    maze_moves = 0;
    int off = (ARENA_W - MAZE_N * MAZE_CELL) / 2;
    for (int i = 0; i < MAZE_N * MAZE_N; i++) {
        lv_obj_t *c = g_box(arena, off + (i % MAZE_N) * MAZE_CELL, off + (i / MAZE_N) * MAZE_CELL, MAZE_CELL, MAZE_CELL, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(c, i == MAZE_N * MAZE_N - 1 ? LV_OPA_50 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(c, g_hue(2), 0);
        lv_obj_set_style_border_width(c, 3, 0);
        lv_obj_set_style_border_color(c, lv_color_make(170, 175, 195), 0);
        lv_border_side_t sides = 0;
        if (walls[i] & N_WALL) sides |= LV_BORDER_SIDE_TOP;
        if (walls[i] & E_WALL) sides |= LV_BORDER_SIDE_RIGHT;
        if (walls[i] & S_WALL) sides |= LV_BORDER_SIDE_BOTTOM;
        if (walls[i] & W_WALL) sides |= LV_BORDER_SIDE_LEFT;
        lv_obj_set_style_border_side(c, sides, 0);
    }
    maze_player = g_box(arena, 0, 0, 18, 18, g_hue(1), CIRCLE);
    maze_place();
    lv_obj_add_event_cb(screen, maze_gesture, LV_EVENT_GESTURE, NULL);
    imu_zero();
    maze_tilt_at = 0;
    g_timer = lv_timer_create(maze_tilt_tick, 80, NULL);
    g_say("Desliza el dedo o inclina para llegar a la meta verde");
    return screen;
}

// ========================================================================================================== Puzle
static lv_obj_t *tile[9];
static uint8_t tiles[9];  // 0 is the blank
static int slide_moves;

static void slide_draw(void) {
    for (int i = 0; i < 9; i++) {
        lv_obj_t *t = lv_obj_get_child(tile[i], 0);
        if (!tiles[i]) {
            lv_obj_set_style_bg_opa(tile[i], LV_OPA_TRANSP, 0);
            lv_label_set_text(t, "");
        } else {
            lv_obj_set_style_bg_opa(tile[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(tile[i], g_hue(tiles[i] - 1), 0);
            lv_label_set_text_fmt(t, "%d", tiles[i]);
        }
    }
}

static void slide_tap(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing || !tiles[i]) return;
    int blank = -1;
    for (int k = 0; k < 9; k++) {
        if (!tiles[k]) blank = k;
    }
    bool next_to = (i / 3 == blank / 3 && (i - blank == 1 || blank - i == 1)) || (i % 3 == blank % 3 && (i - blank == 3 || blank - i == 3));
    if (!next_to) return;
    tiles[blank] = tiles[i];
    tiles[i] = 0;
    slide_moves++;
    slide_draw();
    g_say("Movimientos %d", slide_moves);
    bool solved = true;
    for (int k = 0; k < 8; k++) solved &= tiles[k] == k + 1;
    if (solved) g_finish(G_SLIDE, (uint16_t)slide_moves, "¡Resuelto! movimientos");
}

lv_obj_t *build_slide(void) {
    lv_obj_t *screen = g_frame(G_SLIDE);
    make_arena(screen);
    for (int i = 0; i < 8; i++) tiles[i] = (uint8_t)(i + 1);
    tiles[8] = 0;
    int blank = 8;
    for (int n = 0; n < 160; n++) {  // shuffled by legal moves, so it can always be solved
        int options[4], count = 0;
        if (blank >= 3) options[count++] = blank - 3;
        if (blank < 6) options[count++] = blank + 3;
        if (blank % 3 > 0) options[count++] = blank - 1;
        if (blank % 3 < 2) options[count++] = blank + 1;
        int to = options[g_rand((uint32_t)count)];
        tiles[blank] = tiles[to];
        tiles[to] = 0;
        blank = to;
    }
    slide_moves = 0;
    for (int i = 0; i < 9; i++) {
        tile[i] = g_button(arena, 72, 72, g_hue(0), "", bug_font_body);
        lv_obj_set_pos(tile[i], 4 + (i % 3) * 80, 4 + (i / 3) * 80);
        lv_obj_add_event_cb(tile[i], slide_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    }
    slide_draw();
    g_say("Ordena del 1 al 8");
    return screen;
}

// ====================================================================================================== Burbujas
#define BUB_N 6
#define BUB_SIZE 38
static lv_obj_t *bubble[BUB_N * BUB_N];
static uint8_t bub[BUB_N * BUB_N];  // 0 empty, 1..4 a colour
static int bubble_score;

static int bubble_group(int start, bool *mark) {
    int stack[BUB_N * BUB_N], top = 0, count = 0;
    uint8_t colour = bub[start];
    stack[top++] = start;
    mark[start] = true;
    while (top > 0) {
        int c = stack[--top];
        count++;
        int x = c % BUB_N, y = c / BUB_N;
        int near[4] = {y > 0 ? c - BUB_N : -1, y < BUB_N - 1 ? c + BUB_N : -1, x > 0 ? c - 1 : -1, x < BUB_N - 1 ? c + 1 : -1};
        for (int k = 0; k < 4; k++) {
            int n = near[k];
            if (n >= 0 && !mark[n] && bub[n] == colour) {
                mark[n] = true;
                stack[top++] = n;
            }
        }
    }
    return count;
}

static void bubble_draw(void) {
    for (int i = 0; i < BUB_N * BUB_N; i++) {
        if (bub[i]) {
            lv_obj_set_style_bg_opa(bubble[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(bubble[i], g_hue(bub[i] * 2 - 2), 0);
            lv_obj_add_flag(bubble[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_set_style_bg_opa(bubble[i], LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(bubble[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static bool bubble_moves_left(void) {
    for (int i = 0; i < BUB_N * BUB_N; i++) {
        if (!bub[i]) continue;
        bool mark[BUB_N * BUB_N] = {false};
        if (bubble_group(i, mark) >= 2) return true;
    }
    return false;
}

static void bubble_tap(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing || !bub[i]) return;
    bool mark[BUB_N * BUB_N] = {false};
    int n = bubble_group(i, mark);
    if (n < 2) return;
    for (int k = 0; k < BUB_N * BUB_N; k++) {
        if (mark[k]) bub[k] = 0;
    }
    bubble_score += n * n;
    bubble_draw();
    g_say("Puntos %d", bubble_score);
    if (!bubble_moves_left()) g_finish(G_BUBBLES, (uint16_t)bubble_score, "Sin jugadas · puntos");
}

lv_obj_t *build_bubbles(void) {
    lv_obj_t *screen = g_frame(G_BUBBLES);
    make_arena(screen);
    bubble_score = 0;
    int off = (ARENA_W - BUB_N * BUB_SIZE) / 2;
    do {
        for (int i = 0; i < BUB_N * BUB_N; i++) bub[i] = (uint8_t)(1 + g_rand(4));
    } while (!bubble_moves_left());
    for (int i = 0; i < BUB_N * BUB_N; i++) {
        bubble[i] = g_box(arena, off + (i % BUB_N) * BUB_SIZE + 2, off + (i / BUB_N) * BUB_SIZE + 2, BUB_SIZE - 4, BUB_SIZE - 4, g_hue(0), CIRCLE);
        lv_obj_set_ext_click_area(bubble[i], 2);
        lv_obj_add_event_cb(bubble[i], bubble_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    }
    bubble_draw();
    g_say("Toca un grupo de 2 o más del mismo color");
    return screen;
}

// ========================================================================================================== Dibujo
#define CANVAS_W 244
#define CANVAS_H 168
static lv_obj_t *canvas;
static lv_color_t *canvas_buf;
static int last_x, last_y;
static lv_color_t pen;

static void draw_close(void) {
    heap_caps_free(canvas_buf);
    canvas_buf = NULL;
    canvas = NULL;
}

static void dot(int x, int y) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = pen;
    d.radius = CIRCLE;
    lv_canvas_draw_rect(canvas, x - 5, y - 5, 11, 11, &d);
}

static void draw_touch(lv_event_t *e) {
    if (!canvas) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        last_x = last_y = -1;
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int x = p.x - ARENA_X, y = p.y - ARENA_Y;
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    if (last_x >= 0) {  // fill in the gap since the last point
        int dx = x - last_x, dy = y - last_y, steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 3;
        for (int s = 1; s < steps; s++) dot(last_x + dx * s / steps, last_y + dy * s / steps);
    }
    dot(x, y);
    last_x = x;
    last_y = y;
    lv_obj_invalidate(canvas);
}

static void pen_pick(lv_event_t *e) {
    pen = g_hue((int)(intptr_t)lv_event_get_user_data(e));
}

static void draw_clear(lv_event_t *e) {
    if (canvas) {
        lv_canvas_fill_bg(canvas, lv_color_make(24, 26, 34), LV_OPA_COVER);
        lv_obj_invalidate(canvas);
    }
}

lv_obj_t *build_draw(void) {
    lv_obj_t *screen = g_frame(G_DRAW);
    make_arena(screen);
    canvas_buf = heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H), MALLOC_CAP_SPIRAM);
    g_on_close(draw_close);
    pen = g_hue(1);
    last_x = last_y = -1;
    if (!canvas_buf) {
        g_say("No hay memoria para dibujar");
        return screen;
    }
    canvas = lv_canvas_create(arena);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, lv_color_make(24, 26, 34), LV_OPA_COVER);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, draw_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(canvas, draw_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas, draw_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(canvas, draw_touch, LV_EVENT_PRESS_LOST, NULL);
    for (int i = 0; i < 6; i++) {
        lv_obj_t *s = g_box(arena, 6 + i * 38, CANVAS_H + 10, 30, 30, g_hue(i == 5 ? 7 : i), CIRCLE);
        lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s, 4);
        lv_obj_add_event_cb(s, pen_pick, LV_EVENT_PRESSED, (void *)(intptr_t)(i == 5 ? 7 : i));
    }
    lv_obj_t *clear = g_button(arena, 110, 34, lv_color_make(60, 60, 66), "Borrar", bug_font_small);
    lv_obj_set_pos(clear, ARENA_W / 2 - 55, CANVAS_H + 48);
    lv_obj_add_event_cb(clear, draw_clear, LV_EVENT_SHORT_CLICKED, NULL);
    g_say("Dibuja con el dedo · elige un color");
    return screen;
}

// ========================================================================================================= Figuras
static const struct {
    const char *symbol, *name;
} SHAPES[8] = {{LV_SYMBOL_HOME, "Casa"}, {LV_SYMBOL_BELL, "Campana"}, {LV_SYMBOL_AUDIO, "Bocina"}, {LV_SYMBOL_EYE_OPEN, "Ojo"},
               {LV_SYMBOL_CHARGE, "Rayo"}, {LV_SYMBOL_GPS, "Ubicación"}, {LV_SYMBOL_WIFI, "Wifi"}, {LV_SYMBOL_VIDEO, "Cámara"}};
static lv_obj_t *shape_symbol, *shape_bar, *shape_option[3];
static int shape_answer, shape_lives, shape_score, shape_time, shape_limit;
static int shape_pick[3];

static void shape_round(void) {
    shape_answer = (int)g_rand(8);
    shape_pick[0] = shape_answer;
    for (int k = 1; k < 3; k++) {
        int c, again;
        do {
            c = (int)g_rand(8);
            again = 0;
            for (int j = 0; j < k; j++) again |= shape_pick[j] == c;
        } while (again);
        shape_pick[k] = c;
    }
    for (int i = 2; i > 0; i--) {  // shuffle the three names
        int j = (int)g_rand((uint32_t)(i + 1)), tmp = shape_pick[i];
        shape_pick[i] = shape_pick[j];
        shape_pick[j] = tmp;
    }
    lv_label_set_text(shape_symbol, SHAPES[shape_answer].symbol);
    for (int i = 0; i < 3; i++) lv_label_set_text(lv_obj_get_child(shape_option[i], 0), SHAPES[shape_pick[i]].name);
    shape_limit = 5000 - shape_score * 200;
    if (shape_limit < 2600) shape_limit = 2600;
    shape_time = shape_limit;
}

static void shape_lose(void) {
    shape_lives--;
    if (shape_lives <= 0) {
        g_finish(G_SHAPE, (uint16_t)shape_score, "Fin · aciertos");
        return;
    }
    g_say("Aciertos %d · vidas %d", shape_score, shape_lives);
    shape_round();
}

static void shape_tick(lv_timer_t *t) {
    if (!g_playing) return;
    shape_time -= 50;
    lv_bar_set_value(shape_bar, shape_time * 1000 / shape_limit, LV_ANIM_OFF);
    if (shape_time <= 0) shape_lose();
}

static void shape_choose(lv_event_t *e) {
    if (!g_playing) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (shape_pick[i] == shape_answer) {
        shape_score++;
        g_say("¡Bien! Aciertos %d · vidas %d", shape_score, shape_lives);
        shape_round();
    } else {
        shape_lose();
    }
}

lv_obj_t *build_shape(void) {
    lv_obj_t *screen = g_frame(G_SHAPE);
    make_arena(screen);
    shape_lives = 3;
    shape_score = 0;
    lv_obj_t *disc = g_box(arena, ARENA_W / 2 - 46, 4, 92, 92, lv_color_make(210, 214, 226), CIRCLE);
    shape_symbol = lv_label_create(disc);
    lv_obj_set_style_text_font(shape_symbol, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(shape_symbol, lv_color_make(20, 22, 30), 0);
    lv_obj_center(shape_symbol);
    shape_bar = lv_bar_create(arena);
    lv_obj_set_size(shape_bar, 200, 8);
    lv_obj_set_pos(shape_bar, 22, 104);
    lv_bar_set_range(shape_bar, 0, 1000);
    lv_bar_set_value(shape_bar, 1000, LV_ANIM_OFF);
    for (int i = 0; i < 3; i++) {
        shape_option[i] = g_button(arena, 220, 36, lv_color_make(44, 48, 62), "", bug_font_small);
        lv_obj_set_pos(shape_option[i], 12, 122 + i * 42);
        lv_obj_add_event_cb(shape_option[i], shape_choose, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
    shape_round();
    g_timer = lv_timer_create(shape_tick, 50, NULL);
    g_say("¿Qué es la silueta? Vidas 3");
    return screen;
}

// ======================================================================================================= Preguntas
typedef struct {
    const char *question, *answer[3];
    uint8_t right;
} question_t;

static const question_t QUESTIONS[20] = {
    {"¿Cuál es la capital de Colombia?", {"Bogotá", "Medellín", "Cali"}, 0},
    {"¿Cuántos días tiene una semana?", {"5", "7", "9"}, 1},
    {"¿Qué planeta es el más grande?", {"Marte", "Júpiter", "La Tierra"}, 1},
    {"¿Cuánto es 7 por 8?", {"54", "56", "58"}, 1},
    {"¿Cuál es el río más largo de Sudamérica?", {"Amazonas", "Magdalena", "Orinoco"}, 0},
    {"¿Quién escribió Cien años de soledad?", {"Neruda", "García Márquez", "Borges"}, 1},
    {"¿Cuántos lados tiene un hexágono?", {"5", "6", "8"}, 1},
    {"¿De qué color es el cielo despejado?", {"Azul", "Verde", "Rojo"}, 0},
    {"¿Cuál es el océano más grande?", {"Atlántico", "Índico", "Pacífico"}, 2},
    {"¿Cuántos meses tiene un año?", {"10", "12", "14"}, 1},
    {"¿Qué animal nos da la lana?", {"La vaca", "La oveja", "La gallina"}, 1},
    {"¿Cuál es el símbolo químico del agua?", {"H2O", "CO2", "O2"}, 0},
    {"¿Cuántos continentes hay?", {"5", "7", "9"}, 1},
    {"¿Qué instrumento tiene teclas blancas y negras?", {"Piano", "Tambor", "Flauta"}, 0},
    {"¿En qué país está la Torre Eiffel?", {"Italia", "Francia", "España"}, 1},
    {"¿Cuál es el animal terrestre más grande?", {"Elefante", "Jirafa", "Oso"}, 0},
    {"¿Cuánto es la mitad de 50?", {"20", "25", "30"}, 1},
    {"¿Qué estación viene después del invierno?", {"Otoño", "Verano", "Primavera"}, 2},
    {"¿Qué idioma se habla en Brasil?", {"Español", "Portugués", "Inglés"}, 1},
    {"¿Qué satélite natural tiene la Tierra?", {"La Luna", "Fobos", "Titán"}, 0},
};
static uint8_t quiz_order[20];
static int quiz_index, quiz_score;
static lv_obj_t *quiz_text, *quiz_button[3];

static void quiz_show(void) {
    const question_t *q = &QUESTIONS[quiz_order[quiz_index]];
    lv_label_set_text(quiz_text, q->question);
    for (int i = 0; i < 3; i++) lv_label_set_text(lv_obj_get_child(quiz_button[i], 0), q->answer[i]);
    g_say("Pregunta %d de 10 · aciertos %d", quiz_index + 1, quiz_score);
}

static void quiz_choose(lv_event_t *e) {
    if (!g_playing) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == QUESTIONS[quiz_order[quiz_index]].right) quiz_score++;
    if (++quiz_index >= 10) {
        g_finish(G_QUIZ, (uint16_t)quiz_score, "Terminaste · aciertos");
        return;
    }
    quiz_show();
}

lv_obj_t *build_quiz(void) {
    lv_obj_t *screen = g_frame(G_QUIZ);
    make_arena(screen);
    for (int i = 0; i < 20; i++) quiz_order[i] = (uint8_t)i;
    for (int i = 19; i > 0; i--) {
        int j = (int)g_rand((uint32_t)(i + 1));
        uint8_t tmp = quiz_order[i];
        quiz_order[i] = quiz_order[j];
        quiz_order[j] = tmp;
    }
    quiz_index = 0;
    quiz_score = 0;
    quiz_text = g_label(arena, bug_font_small, COLOR_TEXT_PRIMARY, 226);
    lv_label_set_long_mode(quiz_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(quiz_text, 9, 4);
    lv_obj_set_height(quiz_text, 66);
    for (int i = 0; i < 3; i++) {
        quiz_button[i] = g_button(arena, 226, 42, lv_color_make(44, 48, 62), "", bug_font_small);
        lv_obj_set_pos(quiz_button[i], 9, 84 + i * 50);
        lv_obj_add_event_cb(quiz_button[i], quiz_choose, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
    quiz_show();
    return screen;
}

// ======================================================================================================== Reacción
enum { R_RED, R_GREEN, R_SHOWN };
static lv_obj_t *react_box, *react_text;
static int react_state, react_round, react_wait, react_total;
static uint32_t react_green_at;

static void react_next(void) {
    react_state = R_RED;
    react_wait = 1200 + (int)g_rand(2400);
    lv_obj_set_style_bg_color(react_box, lv_color_make(190, 50, 50), 0);
    lv_label_set_text(react_text, "Espera el verde…");
}

static void react_tick(lv_timer_t *t) {
    if (!g_playing) return;
    if (react_state == R_RED && (react_wait -= 20) <= 0) {
        react_state = R_GREEN;
        react_green_at = lv_tick_get();
        lv_obj_set_style_bg_color(react_box, lv_color_make(34, 197, 94), 0);
        lv_label_set_text(react_text, "¡Ya!");
    } else if (react_state == R_SHOWN && (react_wait -= 20) <= 0) {
        react_next();
    }
}

static void react_tap(lv_event_t *e) {
    if (!g_playing) return;
    if (react_state == R_RED) {  // too early
        react_wait = 1500;
        lv_label_set_text(react_text, "Muy pronto · espera");
        return;
    }
    if (react_state != R_GREEN) return;
    int ms = (int)(lv_tick_get() - react_green_at);
    react_total += ms;
    react_round++;
    react_state = R_SHOWN;
    react_wait = 900;
    lv_label_set_text_fmt(react_text, "%d ms", ms);
    g_say("Ronda %d de 5", react_round);
    if (react_round >= 5) g_finish(G_REACT, (uint16_t)(react_total / 5), "Promedio ms");
}

lv_obj_t *build_react(void) {
    lv_obj_t *screen = g_frame(G_REACT);
    make_arena(screen);
    react_round = 0;
    react_total = 0;
    react_box = g_box(arena, 4, 4, ARENA_W - 8, ARENA_H - 8, lv_color_make(190, 50, 50), 24);
    lv_obj_add_flag(react_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(react_box, react_tap, LV_EVENT_PRESSED, NULL);
    react_text = lv_label_create(react_box);
    lv_obj_set_style_text_font(react_text, bug_font_body, 0);
    lv_obj_set_style_text_color(react_text, lv_color_white(), 0);
    lv_obj_center(react_text);
    react_next();
    g_timer = lv_timer_create(react_tick, 20, NULL);
    g_say("Toca en cuanto se ponga verde · 5 rondas");
    return screen;
}

// ========================================================================================================== Toques
static lv_obj_t *taps_button;
static int taps, taps_time;
static bool taps_started;

static float shake_bank;

static void taps_tick(lv_timer_t *t) {
    float shaken = imu_shake();  // shaking the board counts as taps
    if (g_playing && shaken > 0) {
        shake_bank += shaken;
        while (shake_bank >= 0.8f) {
            shake_bank -= 0.8f;
            taps++;
            taps_started = true;
            lv_label_set_text_fmt(lv_obj_get_child(taps_button, 0), "%d", taps);
        }
    }
    if (!g_playing || !taps_started) return;
    taps_time -= 100;
    if (taps_time % 1000 == 0) g_say("Toques %d · %d s", taps, taps_time / 1000);
    if (taps_time <= 0) g_finish(G_TAPS, (uint16_t)taps, "Fin · toques");
}

static void taps_press(lv_event_t *e) {
    if (!g_playing) return;
    taps_started = true;
    taps++;
    lv_label_set_text_fmt(lv_obj_get_child(taps_button, 0), "%d", taps);
}

lv_obj_t *build_taps(void) {
    lv_obj_t *screen = g_frame(G_TAPS);
    make_arena(screen);
    taps = 0;
    taps_time = 10000;
    taps_started = false;
    taps_button = g_button(arena, 170, 170, g_hue(1), "0", bug_font_huge);
    lv_obj_set_style_radius(taps_button, CIRCLE, 0);
    lv_obj_set_pos(taps_button, (ARENA_W - 170) / 2, 30);
    lv_obj_add_event_cb(taps_button, taps_press, LV_EVENT_PRESSED, NULL);
    g_timer = lv_timer_create(taps_tick, 100, NULL);
    shake_bank = 0;
    g_say("Toca o sacude el aparato lo más rápido que puedas · 10 s");
    return screen;
}

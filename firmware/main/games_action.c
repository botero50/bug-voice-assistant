#include "imu.h"
#include "ui_games_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// The real-time games of the gaming area: Lluvia and Atrapa (falling things), Cuerda, Vuelo, Equilibrio, Ladrillos, Canica,
// Corredor, Globos and Baile. All are touch games drawn with plain shapes; a 33 ms timer moves everything.

#define PI_F 3.14159265f
#define DT_MS 33
#define DT (DT_MS / 1000.0f)
#define CIRCLE LV_RADIUS_CIRCLE

static lv_obj_t *arena;
static bool touching;
static float touch_x, touch_y;  // relative to the arena

static void track_touch(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        touching = false;
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    touch_x = (float)(p.x - ARENA_X);
    touch_y = (float)(p.y - ARENA_Y);
    touching = true;
}

static lv_obj_t *new_arena(lv_obj_t *screen, bool follow) {
    arena = g_arena(screen);
    touching = false;
    touch_x = ARENA_W / 2.0f;
    touch_y = ARENA_H / 2.0f;
    if (follow) {
        lv_obj_add_event_cb(arena, track_touch, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(arena, track_touch, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(arena, track_touch, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(arena, track_touch, LV_EVENT_PRESS_LOST, NULL);
    }
    return arena;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void hide(lv_obj_t *o) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void show(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ================================================================================= Lluvia (drop) and Atrapa (catch)
#define FALL_MAX 8
#define ITEM 26
#define BASKET_W 56
#define BASKET_H 18
#define BASKET_Y (ARENA_H - 26)

typedef struct {
    lv_obj_t *obj;
    float x, y;
    bool good, alive;
} item_t;

static item_t items[FALL_MAX];
static lv_obj_t *basket;
static float basket_x;
static bool lanes_mode;
static game_id_t fall_game;
static int f_score, f_lives, f_time, f_spawn;

static void basket_to(float x) {
    if (lanes_mode) {
        float lane_w = ARENA_W / 5.0f;
        int lane = (int)(x / lane_w);
        lane = lane < 0 ? 0 : (lane > 4 ? 4 : lane);
        basket_x = lane * lane_w + (lane_w - BASKET_W) / 2;
    } else {
        basket_x = clampf(x - BASKET_W / 2.0f, 0, ARENA_W - BASKET_W);
    }
    lv_obj_set_x(basket, (lv_coord_t)basket_x);
}

static void fall_touch(lv_event_t *e) {
    if (!g_playing) return;
    track_touch(e);
    if (touching) basket_to(touch_x);  // a release leaves it where it is
}

static void fall_tick(lv_timer_t *t) {
    if (!g_playing) return;
    f_time -= DT_MS;
    float tilt_right, tilt_down;
    if (!lanes_mode && !touching && imu_tilt(&tilt_right, &tilt_down)) basket_to(basket_x + BASKET_W / 2.0f + tilt_right * 700.0f * DT);
    float speed = 95.0f + f_score * 3.0f;
    if (speed > 210.0f) speed = 210.0f;
    for (int i = 0; i < FALL_MAX; i++) {
        item_t *it = &items[i];
        if (!it->alive) continue;
        it->y += speed * DT;
        float cx = it->x + ITEM / 2.0f;
        if (it->y + ITEM >= BASKET_Y && it->y <= BASKET_Y + BASKET_H && cx >= basket_x - 4 && cx <= basket_x + BASKET_W + 4) {
            if (it->good) f_score++;
            else f_lives--;
            it->alive = false;
            hide(it->obj);
        } else if (it->y > ARENA_H) {
            it->alive = false;
            hide(it->obj);
        } else {
            lv_obj_set_pos(it->obj, (lv_coord_t)it->x, (lv_coord_t)it->y);
        }
    }
    f_spawn -= DT_MS;
    if (f_spawn <= 0) {
        f_spawn = 900 - f_score * 12;
        if (f_spawn < 450) f_spawn = 450;
        for (int i = 0; i < FALL_MAX; i++) {
            item_t *it = &items[i];
            if (it->alive) continue;
            it->alive = true;
            it->good = g_rand(4) != 0;
            it->y = -ITEM;
            if (lanes_mode) {
                float lane_w = ARENA_W / 5.0f;
                it->x = (float)g_rand(5) * lane_w + (lane_w - ITEM) / 2;
            } else {
                it->x = (float)g_rand(ARENA_W - ITEM);
            }
            lv_obj_set_style_bg_color(it->obj, it->good ? (lanes_mode ? g_hue(5) : g_hue(0)) : (lanes_mode ? lv_color_make(120, 120, 130) : lv_color_make(40, 40, 46)), 0);
            lv_obj_set_style_radius(it->obj, it->good ? CIRCLE : (lanes_mode ? 5 : CIRCLE), 0);
            lv_obj_set_style_border_width(it->obj, it->good ? 0 : 2, 0);
            lv_obj_set_style_border_color(it->obj, lv_color_make(220, 60, 60), 0);
            show(it->obj);
            break;
        }
    }
    if (f_time <= 0 || f_lives <= 0) {
        g_finish(fall_game, (uint16_t)f_score, "Fin · puntos");
        return;
    }
    if (f_time % 1000 < DT_MS) g_say("Puntos %d · vidas %d · %d s", f_score, f_lives, f_time / 1000);
}

static lv_obj_t *build_falling(game_id_t game, bool lanes) {
    lv_obj_t *screen = g_frame(game);
    new_arena(screen, false);
    fall_game = game;
    lanes_mode = lanes;
    f_score = 0;
    f_lives = 3;
    f_time = 40000;
    f_spawn = 400;
    if (lanes) {
        for (int i = 1; i < 5; i++) g_box(arena, (int)(i * ARENA_W / 5.0f), 0, 1, ARENA_H, lv_color_make(50, 50, 58), 0);
    }
    for (int i = 0; i < FALL_MAX; i++) {
        items[i].obj = g_box(arena, 0, 0, ITEM, ITEM, g_hue(0), CIRCLE);
        items[i].alive = false;
        hide(items[i].obj);
    }
    basket = g_box(arena, 0, BASKET_Y, BASKET_W, BASKET_H, lv_color_make(245, 158, 11), 6);
    basket_to(ARENA_W / 2.0f);
    lv_obj_add_event_cb(arena, fall_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(arena, fall_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(arena, fall_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(arena, fall_touch, LV_EVENT_PRESS_LOST, NULL);
    if (!lanes) imu_zero();
    g_timer = lv_timer_create(fall_tick, DT_MS, NULL);
    g_say(lanes ? "Atrapa las bayas moradas, esquiva las piedras" : "Arrastra o inclina: atrapa lo rojo, esquiva lo oscuro");
    return screen;
}

lv_obj_t *build_drop(void) {
    return build_falling(G_DROP, true);
}

lv_obj_t *build_catch(void) {
    return build_falling(G_CATCH, false);
}

// ======================================================================================================= Cuerda
static lv_obj_t *rope_dot, *rope_player;
static float rope_a, rope_speed;
static int rope_turn, rope_lives, rope_score;
static uint32_t rope_air_until;
#define ROPE_R 84

static int rope_turn_of(float a) {
    return (int)floorf((a - PI_F / 2) / (2 * PI_F));
}

static void rope_jump(lv_event_t *e) {
    uint32_t now = lv_tick_get();
    if (g_playing && now > rope_air_until + 120) rope_air_until = now + 380;
}

static void rope_tick(lv_timer_t *t) {
    if (!g_playing) return;
    uint32_t now = lv_tick_get();
    rope_a += rope_speed * DT;
    float cx = ARENA_W / 2.0f, cy = ARENA_H / 2.0f + 6;
    lv_obj_set_pos(rope_dot, (lv_coord_t)(cx + ROPE_R * cosf(rope_a) - 8), (lv_coord_t)(cy + ROPE_R * sinf(rope_a) - 8));
    bool air = now < rope_air_until;
    lv_obj_set_pos(rope_player, (lv_coord_t)(cx - 17), (lv_coord_t)(cy - 17 - (air ? 30 : 0)));
    int turn = rope_turn_of(rope_a);
    if (turn != rope_turn) {  // the rope went past the ground
        rope_turn = turn;
        if (air) {
            rope_score++;
            rope_speed += 0.12f;
        } else {
            rope_lives--;
        }
        g_say("Saltos %d · vidas %d", rope_score, rope_lives);
        if (rope_lives <= 0) g_finish(G_ROPE, (uint16_t)rope_score, "Fin · saltos");
    }
}

lv_obj_t *build_rope(void) {
    lv_obj_t *screen = g_frame(G_ROPE);
    new_arena(screen, false);
    rope_speed = 3.4f;
    rope_a = -PI_F / 2;
    rope_turn = rope_turn_of(rope_a);
    rope_lives = 3;
    rope_score = 0;
    rope_air_until = 0;
    lv_obj_t *ring = g_box(arena, ARENA_W / 2 - ROPE_R, ARENA_H / 2 + 6 - ROPE_R, ROPE_R * 2, ROPE_R * 2, lv_color_black(), CIRCLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_make(60, 64, 80), 0);
    g_box(arena, ARENA_W / 2 - 60, ARENA_H / 2 + 6 + ROPE_R - 4, 120, 4, lv_color_make(120, 90, 60), 2);  // the ground
    rope_player = g_box(arena, 0, 0, 34, 34, g_hue(2), CIRCLE);
    rope_dot = g_box(arena, 0, 0, 16, 16, g_hue(1), CIRCLE);
    lv_obj_add_event_cb(arena, rope_jump, LV_EVENT_PRESSED, NULL);
    g_timer = lv_timer_create(rope_tick, DT_MS, NULL);
    g_say("Toca para saltar cuando la cuerda llegue abajo");
    return screen;
}

// ======================================================================================================== Vuelo
#define PIPE_W 34
#define PIPE_GAP 92
#define PIPE_SPACING 130
#define BIRD 24
typedef struct {
    lv_obj_t *top, *bottom;
    float x, gap_y;
    bool passed;
} pipe_t;
static pipe_t pipes[3];
static lv_obj_t *bird;
static float bird_y, bird_v;
static int flap_score;

static void pipe_place(pipe_t *p) {
    p->gap_y = 24.0f + (float)g_rand(ARENA_H - PIPE_GAP - 48);
    p->passed = false;
}

static void flap_tap(lv_event_t *e) {
    if (g_playing) bird_v = -210.0f;
}

static void flap_tick(lv_timer_t *t) {
    if (!g_playing) return;
    bird_v += 620.0f * DT;
    bird_y += bird_v * DT;
    float bx = 50;
    bool dead = bird_y < 0 || bird_y > ARENA_H - BIRD;
    for (int i = 0; i < 3; i++) {
        pipe_t *p = &pipes[i];
        p->x -= 95.0f * DT;
        if (p->x < -PIPE_W) {
            p->x += 3 * PIPE_SPACING;
            pipe_place(p);
        }
        if (!p->passed && p->x + PIPE_W < bx) {
            p->passed = true;
            flap_score++;
            g_say("Tubos %d", flap_score);
        }
        lv_obj_set_pos(p->top, (lv_coord_t)p->x, 0);
        lv_obj_set_height(p->top, (lv_coord_t)p->gap_y);
        lv_obj_set_pos(p->bottom, (lv_coord_t)p->x, (lv_coord_t)(p->gap_y + PIPE_GAP));
        lv_obj_set_height(p->bottom, (lv_coord_t)(ARENA_H - p->gap_y - PIPE_GAP));
        if (bx + BIRD > p->x && bx < p->x + PIPE_W && (bird_y < p->gap_y || bird_y + BIRD > p->gap_y + PIPE_GAP)) dead = true;
    }
    lv_obj_set_pos(bird, (lv_coord_t)bx, (lv_coord_t)bird_y);
    if (dead) g_finish(G_FLAPPY, (uint16_t)flap_score, "Chocaste · tubos");
}

lv_obj_t *build_flappy(void) {
    lv_obj_t *screen = g_frame(G_FLAPPY);
    new_arena(screen, false);
    bird_y = ARENA_H / 2.0f;
    bird_v = 0;
    flap_score = 0;
    for (int i = 0; i < 3; i++) {
        pipes[i].x = 230.0f + i * PIPE_SPACING;
        pipe_place(&pipes[i]);
        pipes[i].top = g_box(arena, 0, 0, PIPE_W, 10, g_hue(2), 4);
        pipes[i].bottom = g_box(arena, 0, 0, PIPE_W, 10, g_hue(2), 4);
    }
    bird = g_box(arena, 50, (int)bird_y, BIRD, BIRD, g_hue(1), CIRCLE);
    lv_obj_add_event_cb(arena, flap_tap, LV_EVENT_PRESSED, NULL);
    g_timer = lv_timer_create(flap_tick, DT_MS, NULL);
    g_say("Toca para aletear entre los tubos");
    return screen;
}

// ==================================================================================================== Equilibrio
#define BAR_HALF 88
static lv_obj_t *bar, *ball;
static lv_point_t bar_points[2];
static float angle, ball_p, ball_v;
static uint32_t balance_start;
static int balance_shown;

static void balance_tick(lv_timer_t *t) {
    if (!g_playing) return;
    float tilt_right, tilt_down;
    float target = touching ? clampf((touch_x / ARENA_W - 0.5f) * 2.0f, -1, 1) * 0.5f
                            : (imu_tilt(&tilt_right, &tilt_down) ? clampf(tilt_right * 1.5f, -0.5f, 0.5f) : 0.0f);
    angle += (target - angle) * 0.14f;
    ball_v += 360.0f * sinf(angle) * DT;
    ball_v *= 0.995f;
    ball_p += ball_v * DT;
    float cx = ARENA_W / 2.0f, py = ARENA_H / 2.0f + 30;
    float c = cosf(angle), s = sinf(angle);
    bar_points[0].x = (lv_coord_t)(cx - BAR_HALF * c);
    bar_points[0].y = (lv_coord_t)(py - BAR_HALF * s);
    bar_points[1].x = (lv_coord_t)(cx + BAR_HALF * c);
    bar_points[1].y = (lv_coord_t)(py + BAR_HALF * s);
    lv_line_set_points(bar, bar_points, 2);
    float bx = cx + ball_p * c + s * 17, by = py + ball_p * s - c * 17;
    lv_obj_set_pos(ball, (lv_coord_t)(bx - 11), (lv_coord_t)(by - 11));
    int seconds = (int)((lv_tick_get() - balance_start) / 1000);
    if (seconds != balance_shown) {
        balance_shown = seconds;
        g_say("Tiempo %d s", seconds);
    }
    if (fabsf(ball_p) > BAR_HALF + 6) g_finish(G_BALANCE, (uint16_t)seconds, "Se cayó · segundos");
}

lv_obj_t *build_balance(void) {
    lv_obj_t *screen = g_frame(G_BALANCE);
    new_arena(screen, true);
    angle = 0;
    ball_p = (float)((int)g_rand(30) - 15);
    ball_v = 0;
    balance_start = lv_tick_get();
    balance_shown = -1;
    bar = lv_line_create(arena);
    lv_obj_set_style_line_width(bar, 10, 0);
    lv_obj_set_style_line_color(bar, lv_color_make(150, 150, 165), 0);
    lv_obj_set_style_line_rounded(bar, true, 0);
    ball = g_box(arena, 0, 0, 22, 22, g_hue(0), CIRCLE);
    g_box(arena, ARENA_W / 2 - 8, ARENA_H / 2 + 36, 16, 40, lv_color_make(90, 90, 100), 4);  // the post
    g_timer = lv_timer_create(balance_tick, DT_MS, NULL);
    imu_zero();
    g_say("Inclina el aparato, o mantén apretado a un lado");
    return screen;
}

// ==================================================================================================== Ladrillos
#define BRICK_COLS 5
#define BRICK_ROWS 4
#define BRICK_W 40
#define BRICK_H 16
#define PADDLE_W 54
#define PADDLE_Y (ARENA_H - 22)
#define BALL_D 12
static lv_obj_t *bricks[BRICK_ROWS * BRICK_COLS], *paddle, *brick_ball;
static bool brick_alive[BRICK_ROWS * BRICK_COLS];
static float bx_, by_, bvx, bvy, paddle_x;
static int brick_left, brick_lives, brick_score;

static void brick_serve(void) {
    bx_ = paddle_x + PADDLE_W / 2.0f - BALL_D / 2.0f;
    by_ = PADDLE_Y - BALL_D - 2;
    bvx = (g_rand(2) ? 1 : -1) * 70.0f;
    bvy = -170.0f;
}

static void brick_tick(lv_timer_t *t) {
    if (!g_playing) return;
    float tilt_right, tilt_down;
    if (touching) paddle_x = clampf(touch_x - PADDLE_W / 2.0f, 0, ARENA_W - PADDLE_W);
    else if (imu_tilt(&tilt_right, &tilt_down)) paddle_x = clampf(paddle_x + tilt_right * 700.0f * DT, 0, ARENA_W - PADDLE_W);
    lv_obj_set_x(paddle, (lv_coord_t)paddle_x);
    bx_ += bvx * DT;
    by_ += bvy * DT;
    if (bx_ < 0) { bx_ = 0; bvx = -bvx; }
    if (bx_ > ARENA_W - BALL_D) { bx_ = ARENA_W - BALL_D; bvx = -bvx; }
    if (by_ < 0) { by_ = 0; bvy = -bvy; }
    if (bvy > 0 && by_ + BALL_D >= PADDLE_Y && by_ + BALL_D <= PADDLE_Y + 14 && bx_ + BALL_D >= paddle_x - 4 && bx_ <= paddle_x + PADDLE_W + 4) {
        bvy = -fabsf(bvy);
        bvx += ((bx_ + BALL_D / 2.0f) - (paddle_x + PADDLE_W / 2.0f)) * 4.0f;
        bvx = clampf(bvx, -190, 190);
    }
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; i++) {
        if (!brick_alive[i]) continue;
        float x = 14 + (i % BRICK_COLS) * (BRICK_W + 4), y = 18 + (i / BRICK_COLS) * (BRICK_H + 6);
        if (bx_ + BALL_D > x && bx_ < x + BRICK_W && by_ + BALL_D > y && by_ < y + BRICK_H) {
            brick_alive[i] = false;
            hide(bricks[i]);
            brick_score++;
            brick_left--;
            bvy = -bvy;
            g_say("Ladrillos %d · vidas %d", brick_score, brick_lives);
            break;
        }
    }
    lv_obj_set_pos(brick_ball, (lv_coord_t)bx_, (lv_coord_t)by_);
    if (brick_left == 0) {
        g_finish(G_BRICK, (uint16_t)(brick_score + 5), "¡Limpiaste todo! puntos");
        return;
    }
    if (by_ > ARENA_H) {
        brick_lives--;
        g_say("Ladrillos %d · vidas %d", brick_score, brick_lives);
        if (brick_lives <= 0) g_finish(G_BRICK, (uint16_t)brick_score, "Fin · puntos");
        else brick_serve();
    }
}

lv_obj_t *build_brick(void) {
    lv_obj_t *screen = g_frame(G_BRICK);
    new_arena(screen, true);
    brick_left = BRICK_ROWS * BRICK_COLS;
    brick_lives = 3;
    brick_score = 0;
    paddle_x = (ARENA_W - PADDLE_W) / 2.0f;
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; i++) {
        brick_alive[i] = true;
        bricks[i] = g_box(arena, 14 + (i % BRICK_COLS) * (BRICK_W + 4), 18 + (i / BRICK_COLS) * (BRICK_H + 6), BRICK_W, BRICK_H, g_hue(i / BRICK_COLS), 4);
    }
    paddle = g_box(arena, (int)paddle_x, PADDLE_Y, PADDLE_W, 10, lv_color_make(220, 220, 230), 5);
    brick_ball = g_box(arena, 0, 0, BALL_D, BALL_D, lv_color_white(), CIRCLE);
    brick_serve();
    g_timer = lv_timer_create(brick_tick, DT_MS, NULL);
    imu_zero();
    g_say("Arrastra el dedo o inclina para mover la paleta");
    return screen;
}

// ======================================================================================================= Canica
static lv_obj_t *marble, *roll_berry[3], *roll_hole[4];
static float mx, my, mvx, mvy, berry_x[3], berry_y[3], hole_x[4], hole_y[4];
static int roll_score, roll_lives, roll_time;

static void roll_place_berry(int i) {
    for (int tries = 0; tries < 20; tries++) {
        berry_x[i] = 14.0f + (float)g_rand(ARENA_W - 28);
        berry_y[i] = 14.0f + (float)g_rand(ARENA_H - 28);
        bool ok = true;
        for (int h = 0; h < 4; h++) {
            float dx = berry_x[i] - hole_x[h], dy = berry_y[i] - hole_y[h];
            if (dx * dx + dy * dy < 40.0f * 40.0f) ok = false;
        }
        if (ok) break;
    }
    lv_obj_set_pos(roll_berry[i], (lv_coord_t)(berry_x[i] - 8), (lv_coord_t)(berry_y[i] - 8));
}

static void roll_tick(lv_timer_t *t) {
    if (!g_playing) return;
    roll_time -= DT_MS;
    float tilt_right, tilt_down;
    if (touching) {
        float ax = clampf((touch_x - mx) * 7.0f, -420, 420), ay = clampf((touch_y - my) * 7.0f, -420, 420);
        mvx += ax * DT;
        mvy += ay * DT;
    } else if (imu_tilt(&tilt_right, &tilt_down)) {
        mvx += tilt_right * 1100.0f * DT;
        mvy += tilt_down * 1100.0f * DT;
    }
    mvx *= 0.965f;
    mvy *= 0.965f;
    mx += mvx * DT;
    my += mvy * DT;
    if (mx < 11) { mx = 11; mvx = -mvx * 0.5f; }
    if (mx > ARENA_W - 11) { mx = ARENA_W - 11; mvx = -mvx * 0.5f; }
    if (my < 11) { my = 11; mvy = -mvy * 0.5f; }
    if (my > ARENA_H - 11) { my = ARENA_H - 11; mvy = -mvy * 0.5f; }
    for (int i = 0; i < 3; i++) {
        float dx = mx - berry_x[i], dy = my - berry_y[i];
        if (dx * dx + dy * dy < 19.0f * 19.0f) {
            roll_score++;
            roll_place_berry(i);
        }
    }
    for (int h = 0; h < 4; h++) {
        float dx = mx - hole_x[h], dy = my - hole_y[h];
        if (dx * dx + dy * dy < 14.0f * 14.0f) {
            roll_lives--;
            mx = ARENA_W / 2.0f;
            my = ARENA_H / 2.0f;
            mvx = mvy = 0;
            break;
        }
    }
    lv_obj_set_pos(marble, (lv_coord_t)(mx - 11), (lv_coord_t)(my - 11));
    if (roll_time <= 0 || roll_lives <= 0) {
        g_finish(G_ROLL, (uint16_t)roll_score, "Fin · bayas");
        return;
    }
    if (roll_time % 1000 < DT_MS) g_say("Bayas %d · vidas %d · %d s", roll_score, roll_lives, roll_time / 1000);
}

lv_obj_t *build_roll(void) {
    lv_obj_t *screen = g_frame(G_ROLL);
    new_arena(screen, true);
    roll_score = 0;
    roll_lives = 3;
    roll_time = 60000;
    mx = ARENA_W / 2.0f;
    my = ARENA_H / 2.0f;
    mvx = mvy = 0;
    for (int h = 0; h < 4; h++) {
        for (int tries = 0; tries < 20; tries++) {
            hole_x[h] = 24.0f + (float)g_rand(ARENA_W - 48);
            hole_y[h] = 24.0f + (float)g_rand(ARENA_H - 48);
            float dx = hole_x[h] - ARENA_W / 2.0f, dy = hole_y[h] - ARENA_H / 2.0f;
            if (dx * dx + dy * dy > 60.0f * 60.0f) break;
        }
        roll_hole[h] = g_box(arena, (int)hole_x[h] - 16, (int)hole_y[h] - 16, 32, 32, lv_color_make(10, 10, 12), CIRCLE);
        lv_obj_set_style_border_width(roll_hole[h], 2, 0);
        lv_obj_set_style_border_color(roll_hole[h], lv_color_make(90, 70, 50), 0);
    }
    for (int i = 0; i < 3; i++) {
        roll_berry[i] = g_box(arena, 0, 0, 16, 16, g_hue(1), CIRCLE);
        roll_place_berry(i);
    }
    marble = g_box(arena, 0, 0, 22, 22, g_hue(3), CIRCLE);
    g_timer = lv_timer_create(roll_tick, DT_MS, NULL);
    imu_zero();
    g_say("Inclina el aparato, o pon el dedo donde quieres que ruede");
    return screen;
}

// ===================================================================================================== Corredor
#define RUN_GROUND (ARENA_H - 44)
#define RUN_PLAYER_X 40
static lv_obj_t *runner, *obstacle[3];
static float run_y, run_v, obs_x[3], obs_h[3];
static bool obs_alive[3], obs_passed[3];
static int run_score, run_lives, run_spawn, run_time;
static uint32_t run_hurt_until;

static void run_jump(lv_event_t *e) {
    if (g_playing && run_y >= RUN_GROUND - 30 - 1) run_v = -360.0f;
}

static void run_tick(lv_timer_t *t) {
    if (!g_playing) return;
    run_time -= DT_MS;
    run_v += 900.0f * DT;
    run_y += run_v * DT;
    if (run_y > RUN_GROUND - 30) {
        run_y = RUN_GROUND - 30;
        run_v = 0;
    }
    float speed = 150.0f + run_score * 4.0f;
    if (speed > 300) speed = 300;
    uint32_t now = lv_tick_get();
    for (int i = 0; i < 3; i++) {
        if (!obs_alive[i]) continue;
        obs_x[i] -= speed * DT;
        lv_obj_set_pos(obstacle[i], (lv_coord_t)obs_x[i], (lv_coord_t)(RUN_GROUND - obs_h[i]));
        if (!obs_passed[i] && obs_x[i] + 20 < RUN_PLAYER_X) {
            obs_passed[i] = true;
            run_score++;
        }
        if (obs_x[i] < -30) {
            obs_alive[i] = false;
            hide(obstacle[i]);
        }
        bool overlap = obs_x[i] < RUN_PLAYER_X + 24 && obs_x[i] + 20 > RUN_PLAYER_X && run_y + 30 > RUN_GROUND - obs_h[i];
        if (overlap && now > run_hurt_until) {
            run_lives--;
            run_hurt_until = now + 900;
        }
    }
    if ((run_spawn -= DT_MS) <= 0) {
        run_spawn = 900 + (int)g_rand(700);
        for (int i = 0; i < 3; i++) {
            if (obs_alive[i]) continue;
            obs_alive[i] = true;
            obs_passed[i] = false;
            obs_x[i] = ARENA_W + 10;
            obs_h[i] = 22.0f + (float)g_rand(22);
            lv_obj_set_size(obstacle[i], 20, (lv_coord_t)obs_h[i]);
            show(obstacle[i]);
            break;
        }
    }
    lv_obj_set_pos(runner, RUN_PLAYER_X, (lv_coord_t)run_y);
    if (now < run_hurt_until) lv_obj_set_style_bg_opa(runner, (now / 100) % 2 ? LV_OPA_30 : LV_OPA_COVER, 0);
    else lv_obj_set_style_bg_opa(runner, LV_OPA_COVER, 0);
    if (run_time % 1000 < DT_MS) g_say("Obstáculos %d · vidas %d", run_score, run_lives);
    if (run_lives <= 0 || run_time <= 0) g_finish(G_RUN, (uint16_t)run_score, "Fin · obstáculos");
}

lv_obj_t *build_run(void) {
    lv_obj_t *screen = g_frame(G_RUN);
    new_arena(screen, false);
    run_y = RUN_GROUND - 30;
    run_v = 0;
    run_score = 0;
    run_lives = 3;
    run_spawn = 600;
    run_time = 90000;
    run_hurt_until = 0;
    g_box(arena, 0, RUN_GROUND, ARENA_W, 3, lv_color_make(120, 90, 60), 1);
    for (int i = 0; i < 3; i++) {
        obs_alive[i] = false;
        obstacle[i] = g_box(arena, 0, 0, 20, 30, lv_color_make(60, 150, 90), 4);
        hide(obstacle[i]);
    }
    runner = g_box(arena, RUN_PLAYER_X, (int)run_y, 24, 30, g_hue(1), 8);
    lv_obj_add_event_cb(arena, run_jump, LV_EVENT_PRESSED, NULL);
    g_timer = lv_timer_create(run_tick, DT_MS, NULL);
    g_say("Toca para saltar los obstáculos · 90 s");
    return screen;
}

// ======================================================================================================== Globos
#define BALLOONS 7
static lv_obj_t *balloon[BALLOONS];
static float balloon_y[BALLOONS], balloon_v[BALLOONS];
static bool balloon_alive[BALLOONS];
static int pop_score, balloon_time, balloon_spawn;

static void balloon_tap(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing || !balloon_alive[i]) return;
    balloon_alive[i] = false;
    hide(balloon[i]);
    pop_score++;
    g_say("Globos %d · %d s", pop_score, balloon_time / 1000);
}

static void balloon_tick(lv_timer_t *t) {
    if (!g_playing) return;
    balloon_time -= DT_MS;
    for (int i = 0; i < BALLOONS; i++) {
        if (!balloon_alive[i]) continue;
        balloon_y[i] -= balloon_v[i] * DT;
        if (balloon_y[i] < -50) {
            balloon_alive[i] = false;
            hide(balloon[i]);
        } else {
            lv_obj_set_y(balloon[i], (lv_coord_t)balloon_y[i]);
        }
    }
    if ((balloon_spawn -= DT_MS) <= 0) {
        balloon_spawn = 420 + (int)g_rand(300);
        for (int i = 0; i < BALLOONS; i++) {
            if (balloon_alive[i]) continue;
            balloon_alive[i] = true;
            balloon_y[i] = ARENA_H;
            balloon_v[i] = 55.0f + (float)g_rand(70) + pop_score * 1.5f;
            lv_obj_set_pos(balloon[i], (lv_coord_t)g_rand(ARENA_W - 44), ARENA_H);
            lv_obj_set_style_bg_color(balloon[i], g_hue((int)g_rand(8)), 0);
            show(balloon[i]);
            break;
        }
    }
    if (balloon_time <= 0) g_finish(G_BALLOON, (uint16_t)pop_score, "Fin · globos");
}

lv_obj_t *build_balloon(void) {
    lv_obj_t *screen = g_frame(G_BALLOON);
    new_arena(screen, false);
    pop_score = 0;
    balloon_time = 30000;
    balloon_spawn = 200;
    for (int i = 0; i < BALLOONS; i++) {
        balloon_alive[i] = false;
        balloon[i] = g_box(arena, 0, 0, 44, 52, g_hue(i), 22);
        lv_obj_add_flag(balloon[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(balloon[i], 8);
        lv_obj_add_event_cb(balloon[i], balloon_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        hide(balloon[i]);
    }
    g_timer = lv_timer_create(balloon_tick, DT_MS, NULL);
    g_say("Revienta los globos · 30 s");
    return screen;
}

// ========================================================================================================= Baile
#define NOTES 48
#define LANES 4
#define LINE_Y (ARENA_H - 52)
#define NOTE_SPEED 130.0f
typedef struct {
    lv_obj_t *obj;
    uint32_t hit_time;
    uint8_t lane;
    bool judged;
} note_t;
static note_t notes[NOTES];
static int dance_score, dance_lives, dance_left;
static uint32_t dance_start;

static void dance_end_check(void) {
    if (dance_left == 0 || dance_lives <= 0) g_finish(G_DANCE, (uint16_t)dance_score, "Fin · puntos");
}

static void dance_tap(lv_event_t *e) {
    int lane = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing) return;
    uint32_t now = lv_tick_get();
    for (int i = 0; i < NOTES; i++) {
        note_t *n = &notes[i];
        if (n->judged || n->lane != lane) continue;
        int32_t d = (int32_t)(now - n->hit_time);
        if (d < -180 || d > 180) continue;
        n->judged = true;
        hide(n->obj);
        dance_left--;
        int pts = (d >= -90 && d <= 90) ? 2 : 1;
        dance_score += pts;
        g_say("%s · puntos %d · vidas %d", pts == 2 ? "¡Perfecto!" : "¡Bien!", dance_score, dance_lives);
        dance_end_check();
        return;
    }
}

static void dance_tick(lv_timer_t *t) {
    if (!g_playing) return;
    uint32_t now = lv_tick_get();
    for (int i = 0; i < NOTES; i++) {
        note_t *n = &notes[i];
        if (n->judged) continue;
        int32_t until = (int32_t)(n->hit_time - now);
        if (until < -180) {  // missed
            n->judged = true;
            hide(n->obj);
            dance_left--;
            dance_lives--;
            g_say("Fallo · puntos %d · vidas %d", dance_score, dance_lives);
            dance_end_check();
            if (!g_playing) return;
            continue;
        }
        float y = LINE_Y - until * NOTE_SPEED / 1000.0f;
        if (y < -24) {
            hide(n->obj);
        } else {
            show(n->obj);
            lv_obj_set_pos(n->obj, n->lane * (ARENA_W / LANES) + (ARENA_W / LANES - 40) / 2, (lv_coord_t)y);
        }
    }
}

lv_obj_t *build_dance(void) {
    lv_obj_t *screen = g_frame(G_DANCE);
    new_arena(screen, false);
    dance_score = 0;
    dance_lives = 5;
    dance_left = NOTES;
    dance_start = lv_tick_get();
    static const char *const ARROW[LANES] = {LV_SYMBOL_LEFT, LV_SYMBOL_DOWN, LV_SYMBOL_UP, LV_SYMBOL_RIGHT};
    int lane_w = ARENA_W / LANES;
    for (int l = 0; l < LANES; l++) {
        lv_obj_t *lane = g_box(arena, l * lane_w, 0, lane_w - 2, ARENA_H, lv_color_make(26, 28, 38), 6);
        lv_obj_add_flag(lane, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lane, dance_tap, LV_EVENT_PRESSED, (void *)(intptr_t)l);
        lv_obj_t *mark = g_box(arena, l * lane_w + 8, LINE_Y, lane_w - 18, 40, lv_color_black(), 8);
        lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(mark, 2, 0);
        lv_obj_set_style_border_color(mark, g_hue(l * 2 + 1), 0);
        lv_obj_t *t = lv_label_create(mark);
        lv_label_set_text(t, ARROW[l]);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(t, g_hue(l * 2 + 1), 0);
        lv_obj_center(t);
    }
    for (int i = 0; i < NOTES; i++) {
        notes[i].lane = (uint8_t)g_rand(LANES);
        notes[i].hit_time = dance_start + 2200 + (uint32_t)i * 640;
        notes[i].judged = false;
        notes[i].obj = g_box(arena, 0, 0, 40, 24, g_hue(notes[i].lane * 2 + 1), 8);
        hide(notes[i].obj);
    }
    g_timer = lv_timer_create(dance_tick, DT_MS, NULL);
    g_say("Toca la columna cuando la nota llegue al marco");
    return screen;
}

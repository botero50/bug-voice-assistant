#include "ui_games.h"
#include "audio_driver.h"
#include "esp_log.h"
#include "games_rules.h"
#include "nvs.h"
#include "ui_games_internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// The gaming area: the menu, the frame every game sits in, and the first five games (Moles, Memory, Higher or lower,
// Simon says, 2048). The others are in games_action.c and games_mind.c. Everything is played by touch on the round
// screen; swipes belong to the games, so the screen-to-screen swipes are off while one is open (see ui_nav.c).

static const char *TAG = "GAMES";

typedef struct {
    const char *id, *name, *cat, *hint;
    bool higher_is_better;
    lv_obj_t *(*build)(void);
    const char *icon;  // a symbol for the card
} game_def_t;

static const game_def_t GAMES[G_COUNT] = {
    {"moles", "Topos", "Fuerza", "Toca rápido", true, build_moles, LV_SYMBOL_HOME},
    {"balloons", "Globos", "Fuerza", "Revienta los globos", true, build_balloon, LV_SYMBOL_UPLOAD},
    {"marble", "Canica", "Fuerza", "Inclina o toca", true, build_roll, LV_SYMBOL_GPS},
    {"maze", "Laberinto", "Fuerza", "Desliza o inclina", false, build_maze, LV_SYMBOL_DIRECTORY},
    {"reaction", "Reacción", "Velocidad", "Toca al verde", false, build_react, LV_SYMBOL_POWER},
    {"taps", "Toques", "Velocidad", "Toca o sacude", true, build_taps, LV_SYMBOL_CHARGE},
    {"drop", "Lluvia", "Velocidad", "Atrapa en 5 carriles", true, build_drop, LV_SYMBOL_DOWNLOAD},
    {"rope", "Cuerda", "Velocidad", "Salta a tiempo", true, build_rope, LV_SYMBOL_LOOP},
    {"flappy", "Vuelo", "Velocidad", "Entre los tubos", true, build_flappy, LV_SYMBOL_EJECT},
    {"catch", "Atrapa", "Defensa", "Arrastra o inclina", true, build_catch, LV_SYMBOL_DRIVE},
    {"balance", "Equilibrio", "Defensa", "Inclina o toca", true, build_balance, LV_SYMBOL_MINUS},
    {"bricks", "Ladrillos", "Defensa", "Rompe todo", true, build_brick, LV_SYMBOL_LIST},
    {"memory", "Memoria", "Defensa", "Encuentra las parejas", false, build_memory, LV_SYMBOL_COPY},
    {"simon", "Simón dice", "Defensa", "Repite la melodía", true, build_simon, LV_SYMBOL_BELL},
    {"shapes", "Figuras", "Mente", "¿Cuál es la silueta?", true, build_shape, LV_SYMBOL_EYE_CLOSE},
    {"quiz", "Preguntas", "Mente", "10 preguntas", true, build_quiz, LV_SYMBOL_FILE},
    {"slide", "Puzle", "Mente", "Ordena las fichas", false, build_slide, LV_SYMBOL_SHUFFLE},
    {"highlow", "Mayor o menor", "Mente", "Cartas del 1 al 9", true, build_highlow, LV_SYMBOL_UP},
    {"riddles", "Adivinanzas", "Mente", "10 adivinanzas", true, build_riddle, LV_SYMBOL_SETTINGS},
    {"hangman", "Ahorcado", "Mente", "Adivina la palabra", true, build_hang, LV_SYMBOL_KEYBOARD},
    {"anagram", "Anagrama", "Mente", "Ordena las letras", true, build_anagram, LV_SYMBOL_REFRESH},
    {"leftright", "Izquierda o derecha", "Diversión", "Adivina el giro", true, build_lr, LV_SYMBOL_LEFT},
    {"dance", "Baile", "Diversión", "Sigue el ritmo", true, build_dance, LV_SYMBOL_AUDIO},
    {"cups", "Escondite", "Diversión", "¿Dónde está la bola?", true, build_hide, LV_SYMBOL_IMAGE},
    {"runner", "Corredor", "Diversión", "Salta los obstáculos", true, build_run, LV_SYMBOL_NEXT},
    {"adivino", "Adivino", "Diversión", "20 preguntas", false, build_aki, LV_SYMBOL_WARNING},
    {"merge", "2048", "Arcade", "Desliza y une", true, build_merge, LV_SYMBOL_PLUS},
    {"bubbles", "Burbujas", "Arcade", "Revienta grupos", true, build_bubbles, LV_SYMBOL_BATTERY_FULL},
    {"draw", "Dibujo", "Arcade", "Pinta con el dedo", true, build_draw, LV_SYMBOL_EDIT},
};

static game_id_t chosen = G_MERGE;
static lv_obj_t *screen_obj = NULL;
static lv_obj_t *info_label = NULL;
static void (*close_hook)(void) = NULL;
bool g_playing = false;
lv_timer_t *g_timer = NULL;

// ---------------------------------------------------------------------------------------------- helpers
uint32_t g_rand(uint32_t n) {
    return n ? esp_random() % n : 0;
}

lv_color_t g_hue(int i) {
    static const uint8_t RGB[8][3] = {{239, 68, 68}, {245, 158, 11}, {34, 197, 94}, {6, 182, 212},
                                      {59, 130, 246}, {168, 85, 247}, {236, 72, 153}, {148, 163, 184}};
    i &= 7;
    return lv_color_make(RGB[i][0], RGB[i][1], RGB[i][2]);
}

bool g_click_ok(void) {
    return ui_nav_click_allowed();
}

void g_beep(float hz, int ms) {
    audio_set_speaker(true);
    audio_beep(hz, ms);
}

void g_on_close(void (*fn)(void)) {
    close_hook = fn;
}

uint16_t g_best(game_id_t g) {
    nvs_handle_t handle;
    uint16_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "gb_%s", GAMES[g].id);
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u16(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

// Keeps the record if this result beats it. True for a new record.
static bool best_offer(game_id_t g, uint16_t result) {
    uint16_t old = g_best(g);
    bool better = old == 0 || (GAMES[g].higher_is_better ? result > old : result < old);
    if (result == 0 || !better) return false;
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "gb_%s", GAMES[g].id);
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u16(handle, key, result);
        nvs_commit(handle);
        nvs_close(handle);
    }
    return true;
}

lv_obj_t *g_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int width) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t *g_button(lv_obj_t *parent, int w, int h, lv_color_t color, const char *text, const lv_font_t *font) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (text) {
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, text);
        lv_obj_set_style_text_font(t, font, 0);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_obj_center(t);
    }
    return b;
}

lv_obj_t *g_box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void g_say(const char *format, ...) {
    if (!info_label) return;
    char text[96];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    lv_label_set_text(info_label, text);
}

bool g_screen_alive(void) {
    return screen_obj != NULL;
}

static void again_cb(lv_event_t *e);

void g_offer_again(void) {
    g_playing = false;
    if (!screen_obj) return;
    lv_obj_t *b = g_button(screen_obj, 140, 44, lv_color_make(34, 150, 80), "Otra vez", bug_font_body);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, ARENA_Y + ARENA_H - 60);
    lv_obj_add_event_cb(b, again_cb, LV_EVENT_SHORT_CLICKED, NULL);
}

static void again_cb(lv_event_t *e) {
    if (!g_click_ok()) return;
    char path[32];
    snprintf(path, sizeof(path), "games?g=%s", GAMES[chosen].id);
    ui_nav_open_screen(path, NULL);
}

void g_finish(game_id_t g, uint16_t result, const char *what) {
    bool record = best_offer(g, result);
    g_say("%s %u%s", what, (unsigned)result, record ? " · ¡récord!" : "");
    g_offer_again();
}

static void leave_cb(lv_event_t *e) {
    if (g_click_ok()) ui_nav_go(UI_SCREEN_GAMES, LV_SCR_LOAD_ANIM_FADE_IN);
}

static void screen_deleted(lv_event_t *e) {
    if (lv_event_get_target(e) != screen_obj) return;
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    if (close_hook) {
        close_hook();
        close_hook = NULL;
    }
    screen_obj = info_label = NULL;
    g_playing = false;
}

lv_obj_t *g_frame(game_id_t g) {
    if (g_timer) {  // the timer of the game that is being left
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    if (close_hook) {
        close_hook();
        close_hook = NULL;
    }
    lv_obj_t *screen = lv_obj_create(NULL);
    screen_obj = screen;
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = g_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 260);
    lv_label_set_text(title, GAMES[g].name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 62);
    info_label = g_label(screen, bug_font_small, COLOR_TEXT_SECONDARY, 280);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_t *leave = g_button(screen, 110, 36, lv_color_make(60, 60, 66), "Salir", bug_font_small);
    lv_obj_align(leave, LV_ALIGN_TOP_MID, 0, ARENA_Y + ARENA_H + 8);
    lv_obj_add_event_cb(leave, leave_cb, LV_EVENT_SHORT_CLICKED, NULL);
    g_playing = true;
    return screen;
}

lv_obj_t *g_arena(lv_obj_t *screen) {
    lv_obj_t *a = lv_obj_create(screen);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, ARENA_W, ARENA_H);
    lv_obj_set_pos(a, ARENA_X, ARENA_Y);
    return a;
}

// ======================================================================================================= Moles
#define MOLE_TIME_MS 25000
static lv_obj_t *hole[9];
static uint8_t hole_state[9];  // 0 empty, 1 mole, 2 bomb
static int hole_left_ms[9], mole_score, mole_lives, mole_time, mole_spawn_in;

static void hole_show(int i) {
    lv_obj_t *t = lv_obj_get_child(hole[i], 0);
    if (hole_state[i] == 1) {
        lv_obj_set_style_bg_color(hole[i], lv_color_make(34, 197, 94), 0);
        lv_label_set_text(t, "+1");
    } else if (hole_state[i] == 2) {
        lv_obj_set_style_bg_color(hole[i], lv_color_make(220, 60, 60), 0);
        lv_label_set_text(t, "X");
    } else {
        lv_obj_set_style_bg_color(hole[i], lv_color_make(70, 52, 40), 0);
        lv_label_set_text(t, "");
    }
}

static void moles_tick(lv_timer_t *t) {
    if (!g_playing) return;
    mole_time -= 100;
    for (int i = 0; i < 9; i++) {
        if (hole_state[i] && (hole_left_ms[i] -= 100) <= 0) {
            hole_state[i] = 0;
            hole_show(i);
        }
    }
    if ((mole_spawn_in -= 100) <= 0) {
        mole_spawn_in = 350 + (int)g_rand(300);
        int i = (int)g_rand(9);
        if (!hole_state[i]) {
            hole_state[i] = g_rand(5) == 0 ? 2 : 1;
            hole_left_ms[i] = 900 + (int)g_rand(400);
            hole_show(i);
        }
    }
    if (mole_time <= 0 || mole_lives <= 0) {
        g_finish(G_MOLES, (uint16_t)mole_score, "Fin · puntos");
        return;
    }
    if (mole_time % 1000 == 0) g_say("Puntos %d · vidas %d · %d s", mole_score, mole_lives, mole_time / 1000);
}

static void hole_tap(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing) return;
    if (hole_state[i] == 1) mole_score++;
    else if (hole_state[i] == 2) mole_lives--;
    else return;
    hole_state[i] = 0;
    hole_show(i);
}

lv_obj_t *build_moles(void) {
    lv_obj_t *screen = g_frame(G_MOLES);
    memset(hole_state, 0, sizeof(hole_state));
    mole_score = 0;
    mole_lives = 3;
    mole_time = MOLE_TIME_MS;
    mole_spawn_in = 300;
    for (int i = 0; i < 9; i++) {
        hole[i] = g_button(screen, 70, 70, lv_color_make(70, 52, 40), "", bug_font_body);
        lv_obj_set_style_radius(hole[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(hole[i], ARENA_X + 5 + (i % 3) * 78, ARENA_Y + 5 + (i / 3) * 78);
        lv_obj_add_event_cb(hole[i], hole_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    }
    g_timer = lv_timer_create(moles_tick, 100, NULL);
    g_say("Toca los verdes, evita las X · 25 s");
    return screen;
}

// ==================================================================================================== Memory
static lv_obj_t *mem_card[16];
static uint8_t mem_face[16];
static bool mem_open[16], mem_done[16];
static int mem_first, mem_moves, mem_pairs;
static bool mem_wait;
static const char *const MEM_SYMBOL[8] = {LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_AUDIO, LV_SYMBOL_VIDEO,
                                          LV_SYMBOL_EYE_OPEN, LV_SYMBOL_CHARGE, LV_SYMBOL_GPS, LV_SYMBOL_WIFI};

static void mem_show(int i) {
    lv_obj_t *t = lv_obj_get_child(mem_card[i], 0);
    if (mem_open[i] || mem_done[i]) {
        lv_obj_set_style_bg_color(mem_card[i], g_hue(mem_face[i]), 0);
        lv_label_set_text(t, MEM_SYMBOL[mem_face[i]]);
    } else {
        lv_obj_set_style_bg_color(mem_card[i], lv_color_make(55, 55, 64), 0);
        lv_label_set_text(t, "?");
    }
}

static void mem_hide_cb(lv_timer_t *t) {
    int a = (int)(intptr_t)t->user_data >> 8, b = (int)(intptr_t)t->user_data & 0xFF;
    mem_open[a] = mem_open[b] = false;
    if (screen_obj) {
        mem_show(a);
        mem_show(b);
    }
    mem_wait = false;
}

static void mem_tap(lv_event_t *e) {
    if (!g_playing || mem_wait) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (mem_open[i] || mem_done[i]) return;
    mem_open[i] = true;
    mem_show(i);
    if (mem_first < 0) {
        mem_first = i;
        return;
    }
    int a = mem_first;
    mem_first = -1;
    mem_moves++;
    g_say("Intentos %d · récord %u", mem_moves, (unsigned)g_best(G_MEMORY));
    if (mem_face[a] == mem_face[i]) {
        mem_done[a] = mem_done[i] = true;
        if (++mem_pairs == 8) g_finish(G_MEMORY, (uint16_t)mem_moves, "¡Lo lograste! intentos");
        return;
    }
    mem_wait = true;
    lv_timer_t *t = lv_timer_create(mem_hide_cb, 700, (void *)(intptr_t)((a << 8) | i));
    lv_timer_set_repeat_count(t, 1);
}

lv_obj_t *build_memory(void) {
    lv_obj_t *screen = g_frame(G_MEMORY);
    for (int i = 0; i < 16; i++) mem_face[i] = (uint8_t)(i / 2);
    for (int i = 15; i > 0; i--) {  // shuffle
        int j = (int)g_rand((uint32_t)(i + 1));
        uint8_t tmp = mem_face[i];
        mem_face[i] = mem_face[j];
        mem_face[j] = tmp;
    }
    memset(mem_open, 0, sizeof(mem_open));
    memset(mem_done, 0, sizeof(mem_done));
    mem_first = -1;
    mem_moves = mem_pairs = 0;
    mem_wait = false;
    for (int i = 0; i < 16; i++) {
        mem_card[i] = g_button(screen, 56, 56, lv_color_make(55, 55, 64), "?", &lv_font_montserrat_28);
        lv_obj_set_pos(mem_card[i], ARENA_X + 2 + (i % 4) * 61, ARENA_Y + 2 + (i / 4) * 61);
        lv_obj_add_event_cb(mem_card[i], mem_tap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
    g_say("Encuentra las 8 parejas");
    return screen;
}

// ============================================================================================ Higher / lower
static int hl_current, hl_lives, hl_score;
static lv_obj_t *hl_card;

static void hl_answer(lv_event_t *e) {
    if (!g_playing) return;
    bool says_higher = (bool)(intptr_t)lv_event_get_user_data(e);
    int next;
    do {
        next = 1 + (int)g_rand(9);
    } while (next == hl_current);
    bool right = says_higher ? next > hl_current : next < hl_current;
    if (right) hl_score++;
    else hl_lives--;
    lv_label_set_text_fmt(hl_card, "%d", next);
    hl_current = next;
    if (hl_lives <= 0) {
        g_finish(G_HIGHLOW, (uint16_t)hl_score, "Fin · puntos");
        return;
    }
    g_say("%s · puntos %d · vidas %d", right ? "¡Bien!" : "Fallaste", hl_score, hl_lives);
}

lv_obj_t *build_highlow(void) {
    lv_obj_t *screen = g_frame(G_HIGHLOW);
    hl_lives = 3;
    hl_score = 0;
    hl_card = g_label(screen, bug_font_huge, COLOR_TEXT_PRIMARY, 120);
    lv_obj_align(hl_card, LV_ALIGN_TOP_MID, 0, 150);
    hl_current = 1 + (int)g_rand(9);
    lv_label_set_text_fmt(hl_card, "%d", hl_current);
    lv_obj_t *up = g_button(screen, 116, 58, lv_color_make(34, 150, 80), "Mayor", bug_font_body);
    lv_obj_align(up, LV_ALIGN_TOP_MID, -66, 252);
    lv_obj_add_event_cb(up, hl_answer, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *down = g_button(screen, 116, 58, lv_color_make(190, 70, 70), "Menor", bug_font_body);
    lv_obj_align(down, LV_ALIGN_TOP_MID, 66, 252);
    lv_obj_add_event_cb(down, hl_answer, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)0);
    g_say("¿El siguiente es mayor o menor? Vidas 3");
    return screen;
}

// ======================================================================================================= Simon
#define SIMON_MAX 40
static lv_obj_t *pad[4];
static uint8_t seq[SIMON_MAX];
static int seq_len, seq_show, seq_input;
static enum { SIMON_SHOW, SIMON_LISTEN } simon_phase;
static int simon_lit_ms;
static const float SIMON_TONE[4] = {330.0f, 262.0f, 392.0f, 523.0f};

static lv_color_t pad_color(int i, bool lit) {
    static const int HUE[4] = {2, 0, 1, 4};
    lv_color_t c = g_hue(HUE[i]);
    return lit ? c : lv_color_mix(c, lv_color_black(), 90);
}

static void simon_light(int i, bool on) {
    lv_obj_set_style_bg_color(pad[i], pad_color(i, on), 0);
    if (on) g_beep(SIMON_TONE[i], 140);
}

static void simon_next_round(void) {
    if (seq_len < SIMON_MAX) seq[seq_len++] = (uint8_t)g_rand(4);
    seq_show = 0;
    seq_input = 0;
    simon_phase = SIMON_SHOW;
    simon_lit_ms = -400;
    g_say("Mira · ronda %d", seq_len);
}

static void simon_tick(lv_timer_t *t) {
    if (!g_playing || simon_phase != SIMON_SHOW) return;
    simon_lit_ms += 100;
    if (simon_lit_ms == 0) {
        simon_light(seq[seq_show], true);
    } else if (simon_lit_ms >= 400) {
        simon_light(seq[seq_show], false);
        simon_lit_ms = -200;
        if (++seq_show >= seq_len) {
            simon_phase = SIMON_LISTEN;
            g_say("Tu turno · ronda %d", seq_len);
        }
    }
}

static void pad_tap(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_playing || simon_phase != SIMON_LISTEN) return;
    simon_light(i, true);
    simon_light(i, false);
    if (i != seq[seq_input]) {
        g_finish(G_SIMON, (uint16_t)(seq_len - 1), "Fallaste · rondas");
        return;
    }
    if (++seq_input >= seq_len) simon_next_round();
}

lv_obj_t *build_simon(void) {
    lv_obj_t *screen = g_frame(G_SIMON);
    seq_len = 0;
    for (int i = 0; i < 4; i++) {
        pad[i] = g_button(screen, 112, 112, pad_color(i, false), NULL, bug_font_body);
        lv_obj_set_style_radius(pad[i], 22, 0);
        lv_obj_set_pos(pad[i], ARENA_X + 6 + (i % 2) * 120, ARENA_Y + 6 + (i / 2) * 120);
        lv_obj_add_event_cb(pad[i], pad_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    }
    simon_next_round();
    g_timer = lv_timer_create(simon_tick, 100, NULL);
    return screen;
}

// ====================================================================================================== 2048
// The rules are in games_rules.h (from TamaPoke's Merge, MIT): a 4x4 board; two equal tiles join into the next number.
static uint8_t board[16];
static uint16_t merge_score;
static lv_obj_t *cells[16], *cell_text[16];

static void merge_draw(void) {
    for (int i = 0; i < 16; i++) {
        if (!board[i]) {
            lv_obj_set_style_bg_color(cells[i], lv_color_make(40, 40, 46), 0);
            lv_label_set_text(cell_text[i], "");
            continue;
        }
        unsigned value = 1u << board[i];
        lv_label_set_text_fmt(cell_text[i], "%u", value);
        lv_obj_set_style_text_font(cell_text[i], value >= 1000 ? bug_font_small : bug_font_body, 0);
        lv_obj_set_style_bg_color(cells[i], lv_color_mix(lv_color_make(245, 158, 11), lv_color_make(80, 90, 120), (uint8_t)(board[i] * 22)), 0);
    }
    g_say("Puntos %u · récord %u", (unsigned)merge_score, (unsigned)g_best(G_MERGE));
}

static void merge_gesture(lv_event_t *e) {
    if (!g_playing) return;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    int dir = d == LV_DIR_LEFT ? 0 : d == LV_DIR_RIGHT ? 1 : d == LV_DIR_TOP ? 2 : d == LV_DIR_BOTTOM ? 3 : -1;
    if (dir < 0) return;
    bool moved = false;
    merge_score = (uint16_t)(merge_score + mergeSlide(board, (uint8_t)dir, &moved));
    if (!moved) return;
    mergeSpawn(board, esp_random());
    merge_draw();
    if (!mergeCanMove(board)) g_finish(G_MERGE, merge_score, "Sin movimientos · puntos");
}

lv_obj_t *build_merge(void) {
    lv_obj_t *screen = g_frame(G_MERGE);
    memset(board, 0, sizeof(board));
    merge_score = 0;
    mergeSpawn(board, esp_random());
    mergeSpawn(board, esp_random());
    for (int i = 0; i < 16; i++) {
        cells[i] = g_box(screen, ARENA_X + 2 + (i % 4) * 61, ARENA_Y + 2 + (i / 4) * 61, 56, 56, lv_color_make(40, 40, 46), 10);
        cell_text[i] = lv_label_create(cells[i]);
        lv_obj_set_style_text_color(cell_text[i], lv_color_white(), 0);
        lv_obj_center(cell_text[i]);
    }
    lv_obj_add_event_cb(screen, merge_gesture, LV_EVENT_GESTURE, NULL);
    merge_draw();
    return screen;
}

// =========================================================================================================== menu
// Two levels, like a game console: first the categories as big tiles, then the games of one category as cards you swipe through,
// with dots under them to show where you are. A card (or its Jugar button) starts the game.

typedef struct {
    const char *name;
    const char *symbol;  // drawn in the big disc
    uint8_t r, g, b;
} category_t;

static const category_t CATEGORIES[] = {
    {"Fuerza", LV_SYMBOL_CHARGE, 239, 84, 68},
    {"Velocidad", LV_SYMBOL_PLAY, 34, 197, 94},
    {"Defensa", LV_SYMBOL_EYE_OPEN, 59, 130, 246},
    {"Mente", LV_SYMBOL_EDIT, 168, 85, 247},
    {"Diversión", LV_SYMBOL_AUDIO, 236, 72, 153},
    {"Arcade", LV_SYMBOL_SHUFFLE, 245, 158, 11},
};
#define CATEGORY_COUNT ((int)(sizeof(CATEGORIES) / sizeof(CATEGORIES[0])))

static int hub_category = -1;  // -1: the categories; otherwise the games of that category
static lv_obj_t *hub_track = NULL, *hub_dots[24];
static int hub_cards = 0, hub_card_ids[24];

static lv_color_t category_color(int c) {
    return lv_color_make(CATEGORIES[c].r, CATEGORIES[c].g, CATEGORIES[c].b);
}

static int category_of(const char *name) {
    for (int c = 0; c < CATEGORY_COUNT; c++) {
        if (strcmp(CATEGORIES[c].name, name) == 0) return c;
    }
    return 0;
}

static int games_in(int category) {
    int n = 0;
    for (int i = 0; i <= G_COUNT; i++) {
        const char *cat = i == G_COUNT ? "Arcade" : GAMES[i].cat;
        n += category_of(cat) == category;
    }
    return n;
}

static void start_game(int i) {
    if (i == G_COUNT) {
        ui_nav_open_screen("triqui", NULL);
        return;
    }
    char path[32];
    snprintf(path, sizeof(path), "games?g=%s", GAMES[i].id);
    ui_nav_open_screen(path, NULL);
}

static void tile_tap(lv_event_t *e) {
    if (!g_click_ok()) return;
    char path[24];
    snprintf(path, sizeof(path), "games?c=%d", (int)(intptr_t)lv_event_get_user_data(e));
    ui_nav_open_screen(path, NULL);
}

static void card_tap(lv_event_t *e) {
    if (g_click_ok()) start_game((int)(intptr_t)lv_event_get_user_data(e));
}

static void hub_leave(lv_event_t *e) {
    if (!g_click_ok()) return;
    if (hub_category >= 0) ui_nav_open_screen("games", NULL);  // back to the categories
    else ui_nav_go(UI_SCREEN_HOME, LV_SCR_LOAD_ANIM_OUT_TOP);
}

#define CARD_W 214
#define CARD_H 262
#define CARD_GAP 14

static void dots_update(lv_event_t *e) {
    if (!hub_track) return;
    int index = (lv_obj_get_scroll_x(hub_track) + (CARD_W + CARD_GAP) / 2) / (CARD_W + CARD_GAP);
    if (index < 0) index = 0;
    if (index >= hub_cards) index = hub_cards - 1;
    lv_color_t accent = category_color(hub_category);
    for (int i = 0; i < hub_cards; i++) {
        lv_obj_set_size(hub_dots[i], i == index ? 18 : 8, 8);
        lv_obj_set_style_bg_color(hub_dots[i], i == index ? accent : lv_color_make(70, 74, 92), 0);
    }
}

static bool hub_bench = false;  // for measuring the cards' scrolling: "games?c=3&bench" scrolls them by itself
static lv_timer_t *bench_timer = NULL;

static void bench_tick(lv_timer_t *t) {
    static int direction = 1, steps = 0;
    if (!hub_track) return;
    lv_obj_scroll_by(hub_track, direction * -14, 0, LV_ANIM_OFF);
    if (++steps >= 40) {
        steps = 0;
        direction = -direction;
    }
}

static void hub_deleted(lv_event_t *e) {
    if (bench_timer) {
        lv_timer_del(bench_timer);
        bench_timer = NULL;
    }
    hub_track = NULL;
    for (int i = 0; i < 24; i++) hub_dots[i] = NULL;
}

static lv_obj_t *build_categories(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, hub_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = g_label(screen, bug_font_body, COLOR_TEXT_PRIMARY, 260);
    lv_label_set_text(title, "Juegos");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 62);
    for (int c = 0; c < CATEGORY_COUNT; c++) {
        lv_color_t color = category_color(c);
        lv_obj_t *tile = lv_btn_create(screen);
        lv_obj_set_size(tile, 138, 88);
        lv_obj_set_pos(tile, 233 - 143 + (c % 2) * 148, 98 + (c / 2) * 98);
        lv_obj_set_style_radius(tile, 22, 0);
        lv_obj_set_style_bg_color(tile, lv_color_mix(color, lv_color_black(), 70), 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, lv_color_mix(color, lv_color_black(), 150), 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_t *disc = g_box(tile, 8, 4, 36, 36, color, CIRCLE_RADIUS);
        lv_obj_t *icon = lv_label_create(disc);
        lv_label_set_text(icon, CATEGORIES[c].symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_center(icon);
        lv_obj_t *name = lv_label_create(tile);
        lv_label_set_text(name, CATEGORIES[c].name);
        lv_obj_set_style_text_font(name, bug_font_small, 0);
        lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_pos(name, 10, 42);
        lv_obj_t *count = lv_label_create(tile);
        lv_label_set_text_fmt(count, "%d juegos", games_in(c));
        lv_obj_set_style_text_font(count, bug_font_small, 0);
        lv_obj_set_style_text_color(count, lv_color_mix(color, lv_color_white(), 150), 0);
        lv_obj_set_pos(count, 10, 62);
        lv_obj_add_event_cb(tile, tile_tap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)c);
    }
    lv_obj_t *leave = g_button(screen, 110, 36, lv_color_make(60, 60, 66), "Salir", bug_font_small);
    lv_obj_align(leave, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_add_event_cb(leave, hub_leave, LV_EVENT_SHORT_CLICKED, NULL);
    return screen;
}

static lv_obj_t *build_carousel(int category) {
    lv_color_t color = category_color(category);
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(screen, hub_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = g_label(screen, bug_font_body, color, 260);
    lv_label_set_text(title, CATEGORIES[category].name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 62);

    // The cards: a row that scrolls sideways and stops with one card in the middle
    lv_obj_t *track = lv_obj_create(screen);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, 466, CARD_H);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_flex_flow(track, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(track, CARD_GAP, 0);
    lv_obj_set_style_pad_left(track, (466 - CARD_W) / 2, 0);
    lv_obj_set_style_pad_right(track, (466 - CARD_W) / 2, 0);
    lv_obj_set_scroll_dir(track, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(track, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(track, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    hub_track = track;
    hub_cards = 0;

    for (int i = 0; i <= G_COUNT && hub_cards < 24; i++) {
        const char *cat = i == G_COUNT ? "Arcade" : GAMES[i].cat;
        if (category_of(cat) != category) continue;
        const char *name = i == G_COUNT ? "Triqui" : GAMES[i].name;
        const char *hint = i == G_COUNT ? "Tú eres la X" : GAMES[i].hint;
        const char *icon = i == G_COUNT ? LV_SYMBOL_CLOSE : GAMES[i].icon;
        hub_card_ids[hub_cards] = i;

        lv_obj_t *card = lv_obj_create(track);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_radius(card, 30, 0);
        lv_obj_set_style_bg_color(card, lv_color_make(26, 28, 40), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_mix(color, lv_color_black(), 120), 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(card, card_tap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *disc = g_box(card, (CARD_W - 84) / 2, 18, 84, 84, color, CIRCLE_RADIUS);
        lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *glyph = lv_label_create(disc);
        lv_label_set_text(glyph, icon);
        lv_obj_set_style_text_font(glyph, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
        lv_obj_center(glyph);

        lv_obj_t *n = g_label(card, bug_font_body, COLOR_TEXT_PRIMARY, CARD_W - 20);
        lv_label_set_text(n, name);
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 116);
        lv_obj_t *h = g_label(card, bug_font_small, COLOR_TEXT_SECONDARY, CARD_W - 20);
        lv_label_set_text(h, hint);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 148);
        lv_obj_t *r = g_label(card, bug_font_small, lv_color_mix(color, lv_color_white(), 160), CARD_W - 20);
        if (i < G_COUNT && g_best((game_id_t)i)) lv_label_set_text_fmt(r, "Récord %u", (unsigned)g_best((game_id_t)i));
        else lv_label_set_text(r, i < G_COUNT ? "Sin récord" : "");
        lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 172);

        lv_obj_t *play = g_button(card, 130, 42, color, LV_SYMBOL_PLAY "  Jugar", bug_font_small);
        lv_obj_set_style_radius(play, 21, 0);
        lv_obj_align(play, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_add_flag(play, LV_OBJ_FLAG_EVENT_BUBBLE);
        hub_cards++;
    }

    // The dots under the cards
    int dots_w = hub_cards * 14;
    lv_obj_t *row = lv_obj_create(screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, dots_w + 24, 14);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 358);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < hub_cards; i++) {
        hub_dots[i] = g_box(row, 0, 0, 8, 8, lv_color_make(70, 74, 92), 4);
    }
    lv_obj_add_event_cb(track, dots_update, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(track, dots_update, LV_EVENT_SCROLL_END, NULL);

    lv_obj_t *leave = g_button(screen, 120, 34, lv_color_make(60, 60, 66), LV_SYMBOL_LEFT "  Juegos", bug_font_small);
    lv_obj_align(leave, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_add_event_cb(leave, hub_leave, LV_EVENT_SHORT_CLICKED, NULL);
    dots_update(NULL);
    if (hub_bench) bench_timer = lv_timer_create(bench_tick, 33, NULL);
    return screen;
}

lv_obj_t *ui_games_hub_build(void) {
    return hub_category >= 0 && hub_category < CATEGORY_COUNT ? build_carousel(hub_category) : build_categories();
}

lv_obj_t *ui_game_build(void) {
    return GAMES[chosen].build();
}

int ui_games_choose(const char *game) {
    hub_category = -1;
    hub_bench = game && strstr(game, "bench") != NULL;
    if (!game || !game[0] || strcmp(game, "hub") == 0) return 0;
    if (game[0] == 'c' && game[1] == '=') {  // "games?c=3": the games of one category
        hub_category = atoi(game + 2);
        return 0;
    }
    if (strcmp(game, "triqui") == 0) return 2;
    for (int i = 0; i < G_COUNT; i++) {
        size_t n = strlen(GAMES[i].id);
        if (strncmp(game, GAMES[i].id, n) == 0 && (game[n] == '\0' || game[n] == '&')) {
            chosen = (game_id_t)i;
            ESP_LOGI(TAG, "Game: %s", GAMES[i].name);
            return 1;
        }
    }
    return 0;
}

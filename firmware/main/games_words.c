#include "aki_data.h"
#include "ui_games_internal.h"
#include <stdio.h>
#include <string.h>

// The word and guessing games of the gaming area: Adivinanzas (riddles), Ahorcado (hangman), Anagrama and Adivino (the board
// guesses the animal you are thinking of, in 20 yes/no questions). The riddles are also told by voice (local_more.c).

#define CIRCLE LV_RADIUS_CIRCLE

static lv_obj_t *arena;

static void show(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void hide(lv_obj_t *o) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void shuffle(uint8_t *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)g_rand((uint32_t)(i + 1));
        uint8_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

// ===================================================================================================== Adivinanzas
typedef struct {
    const char *text, *option[3];
    uint8_t right;
    const char *keyword;  // what to listen for when the answer is said aloud (lowercase, no accents)
} riddle_t;

static const riddle_t RIDDLES[] = {
    {"Blanca por dentro, verde por fuera. Si quieres que te lo diga, espera.", {"La pera", "El limón", "El pepino"}, 0, "pera"},
    {"Oro parece, plata no es. ¿Qué es?", {"El plátano", "El maíz", "El queso"}, 0, "platano"},
    {"Tengo agujas y no sé coser, tengo números y no sé leer.", {"El erizo", "El reloj", "El calendario"}, 1, "reloj"},
    {"Vuelo sin alas, silbo sin boca, azoto sin manos y barro sin escoba.", {"El viento", "El trueno", "La lluvia"}, 0, "viento"},
    {"Tiene dientes y no come, tiene cabeza y no es hombre.", {"El peine", "El ajo", "La sierra"}, 1, "ajo"},
    {"Redondo, redondo, sin tapa ni fondo.", {"El plato", "El anillo", "El balón"}, 1, "anillo"},
    {"En el campo me crié, atada con verdes lazos, y aquel que llora por mí me está partiendo en pedazos.", {"La cebolla", "La zanahoria", "La lechuga"}, 0, "cebolla"},
    {"Chiquito como un ratón y guarda la casa como un león.", {"El candado", "El timbre", "La llave"}, 2, "llave"},
    {"Tiene cuatro patas pero no puede caminar; en ella comes y puedes trabajar.", {"La silla", "La mesa", "La cama"}, 1, "mesa"},
    {"Si me nombras, desaparezco.", {"El silencio", "El eco", "El humo"}, 0, "silencio"},
    {"Cuanto más se seca, más moja.", {"El jabón", "La esponja", "La toalla"}, 2, "toalla"},
    {"Tiene hojas y no es árbol, tiene lomo y no es animal.", {"El cuaderno", "El libro", "La planta"}, 1, "libro"},
    {"Tiene cola y no es animal, vuela y no es ave.", {"El avión", "La cometa", "El globo"}, 1, "cometa"},
    {"Treinta caballos blancos sobre una colina roja: primero chocan, luego golpean, luego se quedan quietos.", {"Los dientes", "Las uñas", "Las nubes"}, 0, "dientes"},
    {"Si lo tienes y lo compartes, ya no lo tienes. ¿Qué es?", {"Un secreto", "Un caramelo", "Un abrazo"}, 0, "secreto"},
    {"Una señorita muy aseñorada, siempre va en coche y siempre mojada.", {"La lengua", "La lluvia", "La uña"}, 0, "lengua"},
    {"Sale de noche, no tiene luz propia y cambia de forma cada semana.", {"El sol", "La luna", "Una estrella"}, 1, "luna"},
    {"Blanca como la nieve, dulce como la miel, y cuando me echas al café me disuelvo.", {"La sal", "La harina", "El azúcar"}, 2, "azucar"},
    {"Cuanto más grande soy, menos se ve de mí.", {"La oscuridad", "La nube", "La sombra"}, 0, "oscuridad"},
    {"Tengo ciudades sin casas, ríos sin agua y montañas sin árboles. ¿Qué soy?", {"Un sueño", "Un mapa", "Un cuadro"}, 1, "mapa"},
};
#define RIDDLE_COUNT ((int)(sizeof(RIDDLES) / sizeof(RIDDLES[0])))

int riddle_count(void) {
    return RIDDLE_COUNT;
}

const char *riddle_text(int i) {
    return RIDDLES[i % RIDDLE_COUNT].text;
}

const char *riddle_answer(int i) {
    const riddle_t *r = &RIDDLES[i % RIDDLE_COUNT];
    return r->option[r->right];
}

const char *riddle_keyword(int i) {
    return RIDDLES[i % RIDDLE_COUNT].keyword;
}

static uint8_t riddle_order[RIDDLE_COUNT];
static int riddle_index, riddle_score;
static lv_obj_t *riddle_label, *riddle_button[3];

static void riddle_show(void) {
    const riddle_t *r = &RIDDLES[riddle_order[riddle_index]];
    lv_label_set_text(riddle_label, r->text);
    for (int i = 0; i < 3; i++) lv_label_set_text(lv_obj_get_child(riddle_button[i], 0), r->option[i]);
    g_say("Adivinanza %d de 10 · aciertos %d", riddle_index + 1, riddle_score);
}

static void riddle_choose(lv_event_t *e) {
    if (!g_playing) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == RIDDLES[riddle_order[riddle_index]].right) riddle_score++;
    if (++riddle_index >= 10) {
        g_finish(G_RIDDLE, (uint16_t)riddle_score, "Terminaste · aciertos");
        return;
    }
    riddle_show();
}

lv_obj_t *build_riddle(void) {
    lv_obj_t *screen = g_frame(G_RIDDLE);
    arena = g_arena(screen);
    for (int i = 0; i < RIDDLE_COUNT; i++) riddle_order[i] = (uint8_t)i;
    shuffle(riddle_order, RIDDLE_COUNT);
    riddle_index = 0;
    riddle_score = 0;
    riddle_label = g_label(arena, bug_font_small, COLOR_TEXT_PRIMARY, 230);
    lv_label_set_long_mode(riddle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(riddle_label, 7, 0);
    lv_obj_set_height(riddle_label, 100);
    for (int i = 0; i < 3; i++) {
        riddle_button[i] = g_button(arena, 226, 40, lv_color_make(44, 48, 62), "", bug_font_small);
        lv_obj_set_pos(riddle_button[i], 9, 104 + i * 46);
        lv_obj_add_event_cb(riddle_button[i], riddle_choose, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
    riddle_show();
    return screen;
}

// ======================================================================================================== Ahorcado
static const struct {
    const char *word, *hint;
} HANG_WORDS[] = {
    {"ELEFANTE", "animal"}, {"JIRAFA", "animal"}, {"MARIPOSA", "animal"}, {"DELFIN", "animal"}, {"CABALLO", "animal"}, {"TORTUGA", "animal"},
    {"CANGURO", "animal"}, {"PINGUINO", "animal"}, {"GUITARRA", "instrumento"}, {"TROMPETA", "instrumento"}, {"TAMBOR", "instrumento"},
    {"PIANO", "instrumento"}, {"CHOCOLATE", "comida"}, {"PLATANO", "fruta"}, {"MANZANA", "fruta"}, {"SANDIA", "fruta"}, {"ZANAHORIA", "verdura"},
    {"EMPANADA", "comida"}, {"AREPA", "comida"}, {"BICICLETA", "transporte"}, {"AVION", "transporte"}, {"CAMION", "transporte"}, {"MOTO", "transporte"},
    {"VENTANA", "casa"}, {"ESCALERA", "casa"}, {"COCINA", "casa"}, {"TELEFONO", "objeto"}, {"RELOJ", "objeto"}, {"MOCHILA", "objeto"},
    {"CUADERNO", "escuela"}, {"LAPIZ", "escuela"}, {"PROFESOR", "escuela"}, {"BOGOTA", "ciudad"}, {"MEDELLIN", "ciudad"}, {"CARTAGENA", "ciudad"},
    {"MONTANA", "naturaleza"}, {"VOLCAN", "naturaleza"}, {"CASCADA", "naturaleza"}, {"PLAYA", "naturaleza"}, {"FUTBOL", "deporte"}, {"CICLISMO", "deporte"},
    {"NATACION", "deporte"}, {"ASTRONAUTA", "oficio"}, {"BOMBERO", "oficio"}, {"MEDICO", "oficio"}, {"COMPUTADOR", "tecnología"},
};
#define HANG_COUNT ((int)(sizeof(HANG_WORDS) / sizeof(HANG_WORDS[0])))
#define HANG_LIVES 6
static uint8_t hang_order[HANG_COUNT];
static int hang_pos, hang_score, hang_errors;
static uint32_t hang_tried;  // bit per letter
static lv_obj_t *hang_word_label, *hang_key[26], *hang_life[HANG_LIVES];

static void hang_draw(bool reveal) {
    const char *word = HANG_WORDS[hang_order[hang_pos]].word;
    char shown[40];
    int o = 0;
    for (const char *c = word; *c && o < 36; c++) {
        bool open = reveal || (hang_tried >> (*c - 'A')) & 1;
        shown[o++] = open ? *c : '_';
        shown[o++] = ' ';
    }
    shown[o] = '\0';
    lv_label_set_text(hang_word_label, shown);
    for (int i = 0; i < HANG_LIVES; i++) lv_obj_set_style_bg_color(hang_life[i], i < hang_errors ? g_hue(0) : lv_color_make(60, 64, 80), 0);
}

static void hang_next(lv_timer_t *t) {
    if (!g_screen_alive()) return;
    hang_pos = (hang_pos + 1) % HANG_COUNT;
    hang_tried = 0;
    hang_errors = 0;
    for (int i = 0; i < 26; i++) {
        lv_obj_clear_state(hang_key[i], LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(hang_key[i], lv_color_make(44, 48, 62), 0);
    }
    hang_draw(false);
    g_say("Una palabra de: %s · aciertos %d", HANG_WORDS[hang_order[hang_pos]].hint, hang_score);
}

static void hang_press(lv_event_t *e) {
    if (!g_playing) return;
    int letter = (int)(intptr_t)lv_event_get_user_data(e);
    if ((hang_tried >> letter) & 1) return;
    hang_tried |= 1u << letter;
    lv_obj_add_state(hang_key[letter], LV_STATE_DISABLED);
    const char *word = HANG_WORDS[hang_order[hang_pos]].word;
    bool hit = strchr(word, 'A' + letter) != NULL;
    lv_obj_set_style_bg_color(hang_key[letter], hit ? g_hue(2) : g_hue(0), 0);
    if (!hit) hang_errors++;
    bool complete = true;
    for (const char *c = word; *c; c++) complete &= (hang_tried >> (*c - 'A')) & 1;
    hang_draw(false);
    if (hang_errors >= HANG_LIVES) {
        hang_draw(true);
        g_finish(G_HANG, (uint16_t)hang_score, "Perdiste · palabras");
    } else if (complete) {
        hang_score++;
        g_say("¡Bien! Palabras %d", hang_score);
        lv_timer_t *t = lv_timer_create(hang_next, 900, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

lv_obj_t *build_hang(void) {
    lv_obj_t *screen = g_frame(G_HANG);
    arena = g_arena(screen);
    for (int i = 0; i < HANG_COUNT; i++) hang_order[i] = (uint8_t)i;
    shuffle(hang_order, HANG_COUNT);
    hang_pos = 0;
    hang_score = 0;
    hang_errors = 0;
    hang_tried = 0;
    hang_word_label = g_label(arena, bug_font_body, COLOR_TEXT_PRIMARY, 244);
    lv_obj_set_pos(hang_word_label, 0, 10);
    for (int i = 0; i < HANG_LIVES; i++) hang_life[i] = g_box(arena, 62 + i * 22, 52, 14, 14, lv_color_make(60, 64, 80), CIRCLE);
    static const int ROW_START[4] = {0, 9, 18, 26};
    for (int row = 0; row < 3; row++) {
        int count = ROW_START[row + 1] - ROW_START[row];
        int x0 = (ARENA_W - (count * 27 - 3)) / 2;
        for (int k = 0; k < count; k++) {
            int letter = ROW_START[row] + k;
            char text[2] = {(char)('A' + letter), '\0'};
            hang_key[letter] = g_button(arena, 24, 32, lv_color_make(44, 48, 62), text, bug_font_small);
            lv_obj_set_style_radius(hang_key[letter], 6, 0);
            lv_obj_set_style_pad_all(hang_key[letter], 0, 0);
            lv_obj_set_pos(hang_key[letter], x0 + k * 27, 86 + row * 38);
            lv_obj_add_event_cb(hang_key[letter], hang_press, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)letter);
        }
    }
    hang_draw(false);
    g_say("Una palabra de: %s · 6 errores", HANG_WORDS[hang_order[0]].hint);
    return screen;
}

// ======================================================================================================= Anagrama
static const char *const ANAGRAMS[] = {"ESCUELA", "VENTANA", "GUITARRA", "MARIPOSA", "PELOTA", "CAMINO", "NARANJA", "PLANETA", "BOSQUE", "CIUDAD",
                                      "AMIGOS", "FAMILIA", "VERANO", "JARDIN", "MUSICA", "COLORES", "CAMISA", "ESTRELLA", "MONTANA", "TORMENTA"};
#define ANA_COUNT ((int)(sizeof(ANAGRAMS) / sizeof(ANAGRAMS[0])))
#define ANA_MAX 9
static uint8_t ana_order[ANA_COUNT];
static int ana_pos, ana_score, ana_time, ana_len, ana_filled;
static char ana_letters[ANA_MAX + 1], ana_slot_char[ANA_MAX + 1];
static int ana_slot_tile[ANA_MAX];
static lv_obj_t *ana_tile[ANA_MAX], *ana_slot[ANA_MAX];

static void ana_new_word(void) {
    const char *word = ANAGRAMS[ana_order[ana_pos % ANA_COUNT]];
    ana_len = (int)strlen(word);
    ana_filled = 0;
    strlcpy(ana_letters, word, sizeof(ana_letters));
    for (int tries = 0; tries < 10; tries++) {  // scramble until it is different
        for (int i = ana_len - 1; i > 0; i--) {
            int j = (int)g_rand((uint32_t)(i + 1));
            char t = ana_letters[i];
            ana_letters[i] = ana_letters[j];
            ana_letters[j] = t;
        }
        if (strcmp(ana_letters, word) != 0) break;
    }
    int box = ana_len > 7 ? 24 : 28, step = box + 4, x0 = (ARENA_W - (ana_len * step - 4)) / 2;
    for (int i = 0; i < ANA_MAX; i++) {
        if (i < ana_len) {
            lv_obj_set_pos(ana_slot[i], x0 + i * step, 34);
            lv_obj_set_size(ana_slot[i], box, 34);
            lv_label_set_text(lv_obj_get_child(ana_slot[i], 0), "");
            show(ana_slot[i]);
            char text[2] = {ana_letters[i], '\0'};
            lv_obj_set_pos(ana_tile[i], x0 + i * step, 100);
            lv_obj_set_size(ana_tile[i], box, 38);
            lv_label_set_text(lv_obj_get_child(ana_tile[i], 0), text);
            lv_obj_clear_state(ana_tile[i], LV_STATE_DISABLED);
            lv_obj_set_style_bg_opa(ana_tile[i], LV_OPA_COVER, 0);
            show(ana_tile[i]);
        } else {
            hide(ana_slot[i]);
            hide(ana_tile[i]);
        }
    }
}

static void ana_next(void) {
    ana_pos++;
    ana_new_word();
}

static void ana_tile_tap(lv_event_t *e) {
    if (!g_playing) return;
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (lv_obj_has_state(ana_tile[t], LV_STATE_DISABLED) || ana_filled >= ana_len) return;
    lv_obj_add_state(ana_tile[t], LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(ana_tile[t], LV_OPA_30, 0);
    ana_slot_tile[ana_filled] = t;
    ana_slot_char[ana_filled] = ana_letters[t];
    char text[2] = {ana_letters[t], '\0'};
    lv_label_set_text(lv_obj_get_child(ana_slot[ana_filled], 0), text);
    ana_filled++;
    if (ana_filled == ana_len) {
        ana_slot_char[ana_len] = '\0';
        if (strcmp(ana_slot_char, ANAGRAMS[ana_order[ana_pos % ANA_COUNT]]) == 0) {
            ana_score++;
            g_say("¡Correcto! Palabras %d · %d s", ana_score, ana_time / 1000);
            ana_next();
        } else {
            g_say("Esa no es · prueba otra vez");
            for (int i = 0; i < ana_len; i++) {
                lv_obj_clear_state(ana_tile[i], LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(ana_tile[i], LV_OPA_COVER, 0);
                lv_label_set_text(lv_obj_get_child(ana_slot[i], 0), "");
            }
            ana_filled = 0;
        }
    }
}

static void ana_undo(lv_event_t *e) {
    if (!g_playing || ana_filled == 0) return;
    ana_filled--;
    int t = ana_slot_tile[ana_filled];
    lv_obj_clear_state(ana_tile[t], LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(ana_tile[t], LV_OPA_COVER, 0);
    lv_label_set_text(lv_obj_get_child(ana_slot[ana_filled], 0), "");
}

static void ana_skip(lv_event_t *e) {
    if (g_playing) ana_next();
}

static void ana_tick(lv_timer_t *t) {
    if (!g_playing) return;
    ana_time -= 100;
    if (ana_time % 1000 == 0) g_say("Palabras %d · %d s", ana_score, ana_time / 1000);
    if (ana_time <= 0) g_finish(G_ANAGRAM, (uint16_t)ana_score, "Fin · palabras");
}

lv_obj_t *build_anagram(void) {
    lv_obj_t *screen = g_frame(G_ANAGRAM);
    arena = g_arena(screen);
    for (int i = 0; i < ANA_COUNT; i++) ana_order[i] = (uint8_t)i;
    shuffle(ana_order, ANA_COUNT);
    ana_pos = 0;
    ana_score = 0;
    ana_time = 90000;
    for (int i = 0; i < ANA_MAX; i++) {
        ana_slot[i] = g_box(arena, 0, 34, 28, 34, lv_color_make(26, 28, 38), 6);
        lv_obj_set_style_border_width(ana_slot[i], 2, 0);
        lv_obj_set_style_border_color(ana_slot[i], lv_color_make(70, 76, 98), 0);
        lv_obj_t *l = lv_label_create(ana_slot[i]);
        lv_obj_set_style_text_font(l, bug_font_body, 0);
        lv_obj_set_style_text_color(l, COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(l);
        ana_tile[i] = g_button(arena, 28, 38, g_hue(i), "", bug_font_body);
        lv_obj_set_style_radius(ana_tile[i], 8, 0);
        lv_obj_set_style_pad_all(ana_tile[i], 0, 0);
        lv_obj_add_event_cb(ana_tile[i], ana_tile_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    }
    lv_obj_t *undo = g_button(arena, 104, 38, lv_color_make(60, 60, 66), "Borrar", bug_font_small);
    lv_obj_set_pos(undo, 10, 170);
    lv_obj_add_event_cb(undo, ana_undo, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *skip = g_button(arena, 104, 38, lv_color_make(60, 60, 66), "Otra", bug_font_small);
    lv_obj_set_pos(skip, 130, 170);
    lv_obj_add_event_cb(skip, ana_skip, LV_EVENT_SHORT_CLICKED, NULL);
    ana_new_word();
    g_timer = lv_timer_create(ana_tick, 100, NULL);
    g_say("Ordena las letras · 90 s");
    return screen;
}

// ========================================================================================================== Adivino
static uint64_t aki_candidates, aki_asked_bits;
static int aki_asked, aki_current_q, aki_guess;
static enum { AKI_INTRO, AKI_ASKING, AKI_GUESSING, AKI_OVER } aki_state;
static lv_obj_t *aki_text, *aki_yes, *aki_no, *aki_maybe, *aki_start;

static int popcount64(uint64_t v) {
    int n = 0;
    while (v) {
        n += (int)(v & 1);
        v >>= 1;
    }
    return n;
}

// The question that splits the animals still possible most evenly, or -1 when none helps
static int aki_best_question(void) {
    int total = popcount64(aki_candidates), best = -1, best_gap = 1000;
    for (int q = 0; q < AKI_QUESTIONS; q++) {
        if ((aki_asked_bits >> q) & 1) continue;
        int yes = 0;
        for (int a = 0; a < AKI_ANIMALS; a++) {
            if (((aki_candidates >> a) & 1) && ((AKI_YES[a] >> q) & 1)) yes++;
        }
        if (yes == 0 || yes == total) continue;
        int gap = abs(2 * yes - total);
        if (gap < best_gap) {
            best_gap = gap;
            best = q;
        }
    }
    return best;
}

static int aki_first_candidate(void) {
    for (int a = 0; a < AKI_ANIMALS; a++) {
        if ((aki_candidates >> a) & 1) return a;
    }
    return -1;
}

static void aki_lose(void) {
    aki_state = AKI_OVER;
    hide(aki_yes);
    hide(aki_no);
    hide(aki_maybe);
    lv_label_set_text(aki_text, "¡Me ganaste! No sé qué animal es.");
    g_say("Otra vez, y piensa en otro animal");
    g_offer_again();
}

static void aki_ask(void) {
    int total = popcount64(aki_candidates);
    if (total == 0) {
        aki_lose();
        return;
    }
    int q = total > 1 && aki_asked < 20 ? aki_best_question() : -1;
    if (q < 0) {  // nothing more to ask: make a guess
        aki_guess = aki_first_candidate();
        aki_state = AKI_GUESSING;
        lv_label_set_text_fmt(aki_text, "¿Es %s?", AKI_NAME[aki_guess]);
        hide(aki_maybe);
        g_say("Pregunta %d · creo que ya sé", aki_asked + 1);
        return;
    }
    aki_current_q = q;
    aki_state = AKI_ASKING;
    lv_label_set_text(aki_text, AKI_QUESTION[q]);
    show(aki_maybe);
    g_say("Pregunta %d de 20", aki_asked + 1);
}

static void aki_answer(lv_event_t *e) {
    int kind = (int)(intptr_t)lv_event_get_user_data(e);  // 0 yes, 1 no, 2 maybe
    if (!g_playing) return;
    if (aki_state == AKI_ASKING) {
        aki_asked_bits |= 1ull << aki_current_q;
        aki_asked++;
        if (kind != 2) {
            for (int a = 0; a < AKI_ANIMALS; a++) {
                bool has = (AKI_YES[a] >> aki_current_q) & 1;
                if ((kind == 0) != has) aki_candidates &= ~(1ull << a);
            }
        }
        aki_ask();
    } else if (aki_state == AKI_GUESSING) {
        if (kind == 0) {
            aki_state = AKI_OVER;
            hide(aki_yes);
            hide(aki_no);
            lv_label_set_text_fmt(aki_text, "¡Lo adiviné! Era %s.", AKI_NAME[aki_guess]);
            g_finish(G_AKI, (uint16_t)(aki_asked > 0 ? aki_asked : 1), "Adiviné en preguntas:");
        } else {
            aki_candidates &= ~(1ull << aki_guess);
            aki_ask();
        }
    }
}

static void aki_begin(lv_event_t *e) {
    if (aki_state != AKI_INTRO) return;
    hide(aki_start);
    show(aki_yes);
    show(aki_no);
    show(aki_maybe);
    aki_ask();
}

lv_obj_t *build_aki(void) {
    lv_obj_t *screen = g_frame(G_AKI);
    arena = g_arena(screen);
    aki_candidates = (AKI_ANIMALS >= 64) ? ~0ull : ((1ull << AKI_ANIMALS) - 1);
    aki_asked_bits = 0;
    aki_asked = 0;
    aki_state = AKI_INTRO;
    aki_text = g_label(arena, bug_font_body, COLOR_TEXT_PRIMARY, 230);
    lv_label_set_long_mode(aki_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(aki_text, 7, 30);
    lv_obj_set_height(aki_text, 100);
    lv_label_set_text(aki_text, "Piensa en un animal y yo lo adivino con 20 preguntas.");
    aki_start = g_button(arena, 150, 48, lv_color_make(34, 150, 80), "Listo", bug_font_body);
    lv_obj_set_pos(aki_start, 47, 150);
    lv_obj_add_event_cb(aki_start, aki_begin, LV_EVENT_SHORT_CLICKED, NULL);
    aki_yes = g_button(arena, 70, 52, lv_color_make(34, 150, 80), "Sí", bug_font_body);
    aki_no = g_button(arena, 70, 52, lv_color_make(190, 70, 70), "No", bug_font_body);
    aki_maybe = g_button(arena, 70, 52, lv_color_make(80, 84, 104), "Quizá", bug_font_small);
    lv_obj_set_pos(aki_yes, 4, 160);
    lv_obj_set_pos(aki_no, 86, 160);
    lv_obj_set_pos(aki_maybe, 168, 160);
    lv_obj_add_event_cb(aki_yes, aki_answer, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(aki_no, aki_answer, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(aki_maybe, aki_answer, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)2);
    hide(aki_yes);
    hide(aki_no);
    hide(aki_maybe);
    g_say("Adivino · 20 preguntas");
    return screen;
}

#include "cJSON.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "nvs.h"
#include "voice_clips.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// More skills the board does by itself, said with the free online voice (tts_online.c):
//  - translation: "cómo se dice casa en inglés", "traduce buenos días al francés" (MyMemory, free), spoken in that language
//  - books: .txt files in /sd/books on the microSD card, read a page at a time ("sigue leyendo" goes on where it stopped)
//  - the morning briefing: "buenos días" gives the date, the weather and today's agenda

static const char *TAG = "MORE";

static bool has(const char *text, const char *word) {
    char padded[48];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// ------------------------------------------------------------------------------------------ words
#define MAX_WORDS 40

typedef struct {
    char words[360];
    const char *at[MAX_WORDS];
    int count;
} words_t;

// Splits on spaces and drops the punctuation around the words
static void split(words_t *w, const char *text) {
    size_t o = 0;
    w->count = 0;
    const char *p = text;
    while (*p && w->count < MAX_WORDS && o + 2 < sizeof(w->words)) {
        while (*p && (*p == ' ' || strchr(",.;:!?\"'", *p) || (unsigned char)*p == 0xC2)) {
            if ((unsigned char)*p == 0xC2 && p[1]) p++;  // the Spanish opening ? and ! are two bytes
            p++;
        }
        if (!*p) break;
        w->at[w->count++] = w->words + o;
        while (*p && *p != ' ' && !strchr(",.;:!?\"", *p) && o + 2 < sizeof(w->words)) w->words[o++] = *p++;
        w->words[o++] = '\0';
    }
}

// Words [from, to) of the original sentence when it splits like the normalized one, else of the normalized one
static void pick_words(const words_t *original, const words_t *plain, int from, int to, char *out, size_t size) {
    const words_t *source = original->count == plain->count ? original : plain;
    out[0] = '\0';
    for (int i = from; i < to && i < source->count; i++) {
        if (out[0]) strlcat(out, " ", size);
        strlcat(out, source->at[i], size);
    }
}

// ---------------------------------------------------------------------------------------- translation
static const struct {
    const char *spoken, *code, *name;
} LANGUAGES[] = {{"ingles", "en", "inglés"}, {"frances", "fr", "francés"}, {"portugues", "pt", "portugués"}, {"italiano", "it", "italiano"},
                 {"aleman", "de", "alemán"}, {"japones", "ja", "japonés"}, {"chino", "zh-CN", "chino"}, {"coreano", "ko", "coreano"}};

static bool try_translation(const char *original, const char *text, local_answer_t *a) {
    static const char *const TRIGGERS[] = {" como se dice ", " como se escribe ", " traduce ", " traducir ", " traduceme ", " traducime ", " que significa ", NULL};
    int trigger = -1;
    for (int i = 0; TRIGGERS[i] && trigger < 0; i++) {
        if (strstr(text, TRIGGERS[i])) trigger = i;
    }
    if (trigger < 0) return false;
    words_t plain, orig;
    split(&plain, text);
    split(&orig, original);

    // where the language is named, and where the trigger phrase ends
    int lang_at = -1, lang = -1;
    for (int i = 0; i < plain.count && lang_at < 0; i++) {
        for (size_t k = 0; k < sizeof(LANGUAGES) / sizeof(LANGUAGES[0]); k++) {
            if (strcmp(plain.at[i], LANGUAGES[k].spoken) == 0) {
                lang_at = i;
                lang = (int)k;
            }
        }
    }
    if (lang < 0) return false;
    int start = 0;  // first word after the trigger
    const char *trigger_text = TRIGGERS[trigger] + 1;
    int trigger_words = 1;
    for (const char *c = trigger_text; *c; c++) trigger_words += *c == ' ' && c[1] != '\0' && c[1] != ' ';
    for (int i = 0; i + trigger_words <= plain.count; i++) {
        char probe[80] = "";
        for (int k = 0; k < trigger_words; k++) {
            if (k) strlcat(probe, " ", sizeof(probe));
            strlcat(probe, plain.at[i + k], sizeof(probe));
        }
        char want[80];
        strlcpy(want, trigger_text, sizeof(want));
        size_t wl = strlen(want);
        while (wl > 0 && want[wl - 1] == ' ') want[--wl] = '\0';
        if (strcmp(probe, want) == 0) {
            start = i + trigger_words;
            break;
        }
    }
    // the sentence is "<trigger> X en <language>" or "<trigger> al <language> X"
    int from, to;
    bool language_first = lang_at == start || (lang_at == start + 1 && (strcmp(plain.at[start], "al") == 0 || strcmp(plain.at[start], "en") == 0 ||
                                                                      strcmp(plain.at[start], "a") == 0 || strcmp(plain.at[start], "el") == 0));
    if (lang_at > start && !language_first) {
        from = start;
        to = lang_at;
        while (to > from && (strcmp(plain.at[to - 1], "en") == 0 || strcmp(plain.at[to - 1], "al") == 0 || strcmp(plain.at[to - 1], "a") == 0 ||
                             strcmp(plain.at[to - 1], "el") == 0 || strcmp(plain.at[to - 1], "al") == 0))
            to--;
    } else {
        from = lang_at + 1;
        to = plain.count;
    }
    char phrase[160];
    pick_words(&orig, &plain, from, to, phrase, sizeof(phrase));
    if (!phrase[0]) return false;

    static char translated[260];
    if (!ls_translate("es", LANGUAGES[lang].code, phrase, translated, sizeof(translated))) {
        ls_say(a, CLIP_SIN_CONEXION);
        ls_finish(a, "No tengo conexión en este momento.", NULL);
        return true;
    }
    ls_say(a, CLIP_SE_DICE);
    ls_speak_lang(a, translated, LANGUAGES[lang].code);
    char shown[300];
    snprintf(shown, sizeof(shown), "%s\n\n%s", phrase, translated);
    char title[40];
    snprintf(title, sizeof(title), "En %s", LANGUAGES[lang].name);
    ls_finish(a, translated, "article");
    a->json = ls_article_json(title, shown, "MyMemory");
    if (!a->json) a->screen[0] = '\0';
    ESP_LOGI(TAG, "%s -> %s (%s)", phrase, translated, LANGUAGES[lang].code);
    return true;
}

// ------------------------------------------------------------------------------------------- books
#define BOOK_FOLDER "/sd/books"
#define PAGE_BYTES 330
#define MAX_BOOKS 30

static char book_names[MAX_BOOKS][64];
static int book_count = 0;
static char current_book[64] = "";
static long current_offset = 0;
static long previous_offset = 0;  // where the page just read started
static bool book_state_loaded = false;

static int compare_books(const void *x, const void *y) {
    return strcasecmp((const char *)x, (const char *)y);
}

static void scan_books(void) {
    book_count = 0;
    DIR *dir = opendir(BOOK_FOLDER);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && book_count < MAX_BOOKS) {
        size_t n = strlen(entry->d_name);
        if (n > 4 && n < sizeof(book_names[0]) && strcasecmp(entry->d_name + n - 4, ".txt") == 0) strlcpy(book_names[book_count++], entry->d_name, sizeof(book_names[0]));
    }
    closedir(dir);
    qsort(book_names, (size_t)book_count, sizeof(book_names[0]), compare_books);
}

static void load_book_state(void) {
    if (book_state_loaded) return;
    book_state_loaded = true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        char packed[100];
        size_t size = sizeof(packed);
        if (nvs_get_str(handle, "book", packed, &size) == ESP_OK) {
            char *bar = strrchr(packed, '|');
            if (bar) {
                *bar = '\0';
                strlcpy(current_book, packed, sizeof(current_book));
                current_offset = atol(bar + 1);
            }
        }
        nvs_close(handle);
    }
}

static void save_book_state(void) {
    char packed[100];
    snprintf(packed, sizeof(packed), "%s|%ld", current_book, current_offset);
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "book", packed);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void plain_name(const char *file, char *out, size_t size) {
    size_t o = 0;
    for (const char *p = file; *p && o + 1 < size; p++) {
        if (*p == '.' && strcasecmp(p, ".txt") == 0) break;
        char c = *p == '_' ? ' ' : *p;
        if ((unsigned char)c < 0x80) c = (char)tolower((unsigned char)c);
        out[o++] = c;
    }
    out[o] = '\0';
}

// Reads the page that starts at current_offset into `page` (and moves the offset to the end of it). False at the end.
static bool read_page(char *page, size_t size, long *next_offset) {
    char path[130];
    snprintf(path, sizeof(path), BOOK_FOLDER "/%s", current_book);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, current_offset, SEEK_SET);
    char raw[PAGE_BYTES * 2 + 8];
    size_t got = fread(raw, 1, PAGE_BYTES * 2, file);
    fclose(file);
    if (got == 0) return false;
    raw[got] = '\0';
    // tidy: blank lines and line ends become single spaces, a byte order mark goes
    char tidy[sizeof(raw)];
    size_t o = 0;
    const char *p = raw;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    for (; *p; p++) {
        char c = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
        if (c == ' ' && (o == 0 || tidy[o - 1] == ' ')) continue;
        tidy[o++] = c;
        if (o >= PAGE_BYTES + 60) break;
    }
    tidy[o] = '\0';
    if (o == 0) return false;
    // the page ends at the last sentence end (or, failing that, the last space) that fits
    size_t cut = o;
    if (o > PAGE_BYTES) {
        cut = 0;
        for (size_t i = 0; i < PAGE_BYTES; i++) {
            if ((tidy[i] == '.' || tidy[i] == '!' || tidy[i] == '?') && tidy[i + 1] == ' ') cut = i + 1;
        }
        if (cut < PAGE_BYTES / 3) {
            cut = PAGE_BYTES;
            while (cut > 0 && tidy[cut] != ' ') cut--;
        }
    }
    while (cut > 0 && cut < o && ((unsigned char)tidy[cut] & 0xC0) == 0x80) cut--;
    memcpy(page, tidy, cut < size - 1 ? cut : size - 1);
    page[cut < size - 1 ? cut : size - 1] = '\0';
    // how many bytes of the file that took: walk the raw text again until `cut` tidy bytes were produced
    size_t made = 0, consumed = (unsigned char)raw[0] == 0xEF ? 3 : 0;
    bool last_space = true;
    for (const char *q = raw + consumed; *q && made < cut; q++, consumed++) {
        char c = (*q == '\n' || *q == '\r' || *q == '\t') ? ' ' : *q;
        if (c == ' ' && last_space) continue;
        last_space = c == ' ';
        made++;
    }
    *next_offset = current_offset + (long)consumed;
    return true;
}

static bool say_page(local_answer_t *a, const char *title) {
    static char page[PAGE_BYTES + 8];
    long next = 0;
    if (!read_page(page, sizeof(page), &next)) {
        ls_finish(a, "Terminamos el libro.", NULL);
        ls_say(a, CLIP_LISTO);
        current_offset = 0;  // the next time starts again
        save_book_state();
        return true;
    }
    ls_speak(a, page);
    ls_finish(a, "Leyendo.", "article");
    a->json = ls_article_json(title, page, "Dime: sigue leyendo");
    if (!a->json) a->screen[0] = '\0';
    previous_offset = current_offset;
    current_offset = next;
    save_book_state();
    return true;
}

static bool try_books(const char *text, local_answer_t *a) {
    bool continue_reading = has(text, "sigue") || has(text, "continua") || has(text, "continuar") || has(text, "siguiente") || has(text, "adelante");
    bool again = has(text, "repite") || has(text, "repetir") || has(text, "otra vez");
    bool read = has(text, "lee") || has(text, "leer") || has(text, "leeme") || has(text, "leame") || has(text, "lea") || has(text, "abre") || has(text, "quiero");
    bool about_books = has(text, "libro") || has(text, "libros");
    bool list = about_books && (has(text, "mis") || has(text, "tengo") || has(text, "cuales") || has(text, "hay"));
    bool leyendo = has(text, "leyendo") || (about_books && (continue_reading || again));
    if (!(leyendo || (about_books && (read || list)))) return false;
    if (strstr(text, " leyendo ") == NULL && !about_books) return false;

    scan_books();
    if (book_count == 0) return false;  // the PC may have books of its own
    load_book_state();

    if (list) {
        char names[400] = "";
        for (int i = 0; i < book_count && i < 8; i++) {
            char name[64];
            plain_name(book_names[i], name, sizeof(name));
            if (names[0]) strlcat(names, "\n", sizeof(names));
            strlcat(names, name, sizeof(names));
        }
        char spoken[200];
        snprintf(spoken, sizeof(spoken), "Tienes %d libro%s. Dime cuál quieres que lea.", book_count, book_count == 1 ? "" : "s");
        ls_speak(a, spoken);
        ls_finish(a, spoken, "article");
        a->json = ls_article_json("Tus libros", names, "Dime: lee el libro y el nombre");
        if (!a->json) a->screen[0] = '\0';
        return true;
    }

    char title[64];
    if ((continue_reading || again) && current_book[0]) {
        // go on (or back to the page that was just read)
        plain_name(current_book, title, sizeof(title));
        if (again) current_offset = previous_offset;
        return say_page(a, title);
    }
    if (continue_reading || again) {
        ls_say(a, CLIP_NO_LEYENDO);
        ls_finish(a, "No estás leyendo ningún libro. Dime: lee un libro.", NULL);
        return true;
    }

    // "lee el libro X": the book whose name has the words that were said; any book if none
    static const char *const SKIP[] = {"lee", "leer", "leeme", "leame", "lea", "abre", "quiero", "el", "la", "un", "una", "libro", "libros", "de", "del", "por", "favor", "hola", "bug", NULL};
    words_t plain;
    split(&plain, text);
    int chosen = -1, best = 0;
    for (int b = 0; b < book_count; b++) {
        char name[64];
        plain_name(book_names[b], name, sizeof(name));
        int score = 0;
        for (int i = 0; i < plain.count; i++) {
            bool skip = false;
            for (int k = 0; SKIP[k]; k++) skip |= strcmp(plain.at[i], SKIP[k]) == 0;
            if (!skip && strlen(plain.at[i]) > 2 && strstr(name, plain.at[i])) score++;
        }
        if (score > best) {
            best = score;
            chosen = b;
        }
    }
    if (chosen < 0) chosen = 0;
    if (strcmp(current_book, book_names[chosen]) != 0) {
        strlcpy(current_book, book_names[chosen], sizeof(current_book));
        current_offset = 0;
    }
    plain_name(current_book, title, sizeof(title));
    return say_page(a, title);
}

// ------------------------------------------------------------------------------- morning briefing
static bool try_briefing(const char *text, local_answer_t *a) {
    if (!(strstr(text, " buenos dias ") || strstr(text, " buen dia ") || strstr(text, " resumen del dia ") || strstr(text, " que hay para hoy ") ||
          strstr(text, " como amanece el dia ") || strstr(text, " dame el resumen "))) {
        return false;
    }
    time_t now = time(NULL);
    if (now < 1700000000) return false;
    struct tm t;
    localtime_r(&now, &t);
    static const char *const DAYS[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
    static const char *const MONTHS[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
    char spoken[420];
    int used = snprintf(spoken, sizeof(spoken), "%s. Hoy es %s %d de %s. ", t.tm_hour < 12 ? "Buenos días" : t.tm_hour < 19 ? "Buenas tardes" : "Buenas noches",
                        DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    char piece[200];
    if (local_weather_brief(piece, sizeof(piece)) && used + (int)strlen(piece) + 2 < (int)sizeof(spoken)) {
        used += snprintf(spoken + used, sizeof(spoken) - (size_t)used, "%s ", piece);
    }
    if (local_calendar_brief(piece, sizeof(piece)) && used + (int)strlen(piece) + 2 < (int)sizeof(spoken)) {
        snprintf(spoken + used, sizeof(spoken) - (size_t)used, "%s", piece);
    }
    ls_speak(a, spoken);
    ls_finish(a, "Tu resumen del día.", "article");
    a->json = ls_article_json("Buenos días", spoken, "");
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// ---------------------------------------------------------------------------------------------- riddles by voice
// "Dime una adivinanza" is told aloud (nothing is shown); "la respuesta" tells the answer, and saying the answer is checked.
static int riddle_current = -1;
static uint32_t riddle_told_at = 0;

static bool try_riddle(const char *text, local_answer_t *a) {
    uint32_t now = (uint32_t)(time(NULL));
    bool active = riddle_current >= 0 && now - riddle_told_at < 300;
    bool asks = has(text, "adivinanza") || has(text, "adivinanzas") || (active && (has(text, "otra") || strstr(text, " otra adivinanza ")));
    if (asks && (has(text, "juguemos") || has(text, "jugar") || has(text, "juego") || has(text, "juegos"))) return false;  // the game
    if (asks) {
        int next = (int)(esp_random() % (uint32_t)riddle_count());
        if (next == riddle_current) next = (next + 1) % riddle_count();
        riddle_current = next;
        riddle_told_at = now;
        char spoken[420];
        snprintf(spoken, sizeof(spoken), "%s ¿Qué es?", riddle_text(next));
        ls_speak(a, spoken);
        ls_finish(a, "Una adivinanza.", NULL);
        return true;
    }
    if (!active) return false;
    if (has(text, "respuesta") || has(text, "rindo") || strstr(text, " no se ") || has(text, "dime")) {
        char spoken[160];
        snprintf(spoken, sizeof(spoken), "La respuesta es: %s.", riddle_answer(riddle_current));
        ls_speak(a, spoken);
        ls_finish(a, "La respuesta.", NULL);
        riddle_current = -1;
        return true;
    }
    if (strstr(text, riddle_keyword(riddle_current))) {
        char spoken[160];
        snprintf(spoken, sizeof(spoken), "¡Correcto! %s.", riddle_answer(riddle_current));
        ls_speak(a, spoken);
        ls_finish(a, "¡Correcto!", NULL);
        riddle_current = -1;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------------------- gaming area
static bool try_games(const char *text, local_answer_t *a) {
    bool wants = has(text, "juguemos") || has(text, "jugar") || has(text, "juegos") || has(text, "juego") || has(text, "minijuegos") ||
                 has(text, "minijuego") || has(text, "jueguemos") || strstr(text, " vamos a jugar ");
    static const struct {
        const char *id;
        const char *words[4];  // any of these (padded with spaces) names the game
    } NAMES[] = {
        {"merge", {" 2048 ", " dos mil cuarenta y ocho ", NULL}},
        {"memory", {" memoria ", " parejas ", NULL}},
        {"highlow", {" mayor o menor ", " mayor menor ", " cartas ", NULL}},
        {"moles", {" topos ", " topo ", NULL}},
        {"simon", {" simon ", " simon dice ", NULL}},
        {"balloons", {" globos ", " globo ", NULL}},
        {"marble", {" canica ", " canicas ", " bayas ", NULL}},
        {"maze", {" laberinto ", NULL}},
        {"reaction", {" reaccion ", " reflejos ", NULL}},
        {"taps", {" toques ", " tocar rapido ", NULL}},
        {"drop", {" lluvia ", " carriles ", NULL}},
        {"rope", {" cuerda ", " saltar la cuerda ", NULL}},
        {"flappy", {" vuelo ", " flappy ", " pajaro ", NULL}},
        {"catch", {" atrapa ", " atrapar ", NULL}},
        {"balance", {" equilibrio ", " balanza ", NULL}},
        {"bricks", {" ladrillos ", " rompeladrillos ", NULL}},
        {"shapes", {" figuras ", " siluetas ", " silueta ", NULL}},
        {"quiz", {" preguntas ", " quiz ", " cuestionario ", NULL}},
        {"slide", {" puzle ", " rompecabezas ", " puzzle ", NULL}},
        {"leftright", {" izquierda o derecha ", " izquierda derecha ", NULL}},
        {"dance", {" baile ", " bailar ", " ritmo ", NULL}},
        {"cups", {" escondite ", " vasos ", " donde esta la bola ", NULL}},
        {"runner", {" corredor ", " correr ", NULL}},
        {"bubbles", {" burbujas ", " burbuja ", NULL}},
        {"draw", {" dibujo ", " dibujar ", " pintar ", NULL}},
        {"riddles", {" adivinanzas ", NULL}},
        {"hangman", {" ahorcado ", NULL}},
        {"anagram", {" anagrama ", " anagramas ", NULL}},
        {"adivino", {" akinator ", " adivino ", " veinte preguntas ", " 20 preguntas ", NULL}},
    };
    const char *game = NULL;
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]) && !game; i++) {
        for (int k = 0; NAMES[i].words[k] && !game; k++) {
            if (strstr(text, NAMES[i].words[k])) game = NAMES[i].id;
        }
    }
    if (!wants && !game) return false;
    if (!game && (has(text, "trivia") || has(text, "triqui") || has(text, "akinator") || has(text, "adivina"))) return false;  // other skills
    if (game && !wants && !has(text, "pon") && !has(text, "abre") && !has(text, "quiero")) return false;
    // "pon música" or "quiero escuchar" are not games even if a game word is nearby
    if (has(text, "musica") || has(text, "radio") || has(text, "emisora")) return false;
    char screen[40];
    snprintf(screen, sizeof(screen), game ? "games?g=%s" : "games", game);
    ls_say(a, CLIP_JUGUEMOS);
    ls_finish(a, "Juguemos.", screen);
    return true;
}

bool local_more_try(const char *original, const char *text, local_answer_t *a) {
    if (try_riddle(text, a)) return true;
    if (try_games(text, a)) return true;
    if (try_translation(original, text, a)) return true;
    if (try_briefing(text, a)) return true;
    return try_books(text, a);
}

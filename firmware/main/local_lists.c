#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "nvs.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The shopping list and the notes, kept by the board. The words you say are stored as they were heard and shown
// on the screen; the board's voice cannot read them aloud (it only has recorded words), so it confirms with a
// fixed phrase and the number of items.

static const char *TAG = "LISTS";

#define LISTS 2          // 0 shopping, 1 notes
#define MAX_ITEMS 20
#define ITEM_BYTES 44
#define MAX_WORDS 40

typedef struct {
    char items[LISTS][MAX_ITEMS][ITEM_BYTES];
    uint8_t count[LISTS];
} store_t;

static store_t store;
static bool loaded = false;

static void load(void) {
    if (loaded) return;
    loaded = true;
    memset(&store, 0, sizeof(store));
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(store);
        if (nvs_get_blob(handle, "lists", &store, &size) != ESP_OK || size != sizeof(store)) {
            memset(&store, 0, sizeof(store));
        }
        nvs_close(handle);
    }
}

static void save(void) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save the lists");
        return;
    }
    nvs_set_blob(handle, "lists", &store, sizeof(store));
    nvs_commit(handle);
    nvs_close(handle);
}

// -------------------------------------------------------------------------------------- words
typedef struct {
    char original[MAX_WORDS][32];  // as heard, with accents and capitals, without the comma
    char plain[MAX_WORDS][32];     // lowercase, no accents
    bool comma_after[MAX_WORDS];
    int n;
} words_t;

static void fold(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < size; p++) {
        char c = 0;
        if (*p == 0xC3 && p[1]) {
            switch (p[1]) {
            case 0xA1: case 0x81: c = 'a'; break;
            case 0xA9: case 0x89: c = 'e'; break;
            case 0xAD: case 0x8D: c = 'i'; break;
            case 0xB3: case 0x93: c = 'o'; break;
            case 0xBA: case 0x9A: case 0xBC: case 0x9C: c = 'u'; break;
            case 0xB1: case 0x91: c = 'n'; break;
            default: c = 0;
            }
            p++;
        } else if (isalnum(*p)) {
            c = (char)tolower(*p);
        }
        if (c) out[o++] = c;
    }
    out[o] = '\0';
}

static void split_words(const char *text, words_t *w) {
    w->n = 0;
    const char *p = text;
    while (*p && w->n < MAX_WORDS) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != ' ') end++;
        size_t len = (size_t)(end - p);
        char raw[32];
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        memcpy(raw, p, len);
        raw[len] = '\0';
        bool comma = len > 0 && (raw[len - 1] == ',' || raw[len - 1] == ';');
        // strip punctuation at both ends of the original word
        size_t start = 0, stop = len;
        while (start < stop && strchr(",.;:!?¡¿\"'", raw[start]) && (unsigned char)raw[start] < 0x80) start++;
        while (stop > start && strchr(",.;:!?\"'", raw[stop - 1]) && (unsigned char)raw[stop - 1] < 0x80) stop--;
        // "¿" and "¡" are two bytes in UTF-8
        if (stop - start >= 2 && (unsigned char)raw[start] == 0xC2 && ((unsigned char)raw[start + 1] == 0xBF || (unsigned char)raw[start + 1] == 0xA1)) start += 2;
        memcpy(w->original[w->n], raw + start, stop - start);
        w->original[w->n][stop - start] = '\0';
        fold(w->original[w->n], w->plain[w->n], sizeof(w->plain[w->n]));
        w->comma_after[w->n] = comma;
        if (w->plain[w->n][0]) w->n++;
        p = end;
    }
}

static bool has_word(const words_t *w, const char *word) {
    for (int i = 0; i < w->n; i++) {
        if (!strcmp(w->plain[i], word)) return true;
    }
    return false;
}

static bool is_one_of(const char *word, const char *const *list) {
    for (; *list; list++) {
        if (!strcmp(word, *list)) return true;
    }
    return false;
}

static bool has_any(const words_t *w, const char *const *list) {
    for (int i = 0; i < w->n; i++) {
        if (is_one_of(w->plain[i], list)) return true;
    }
    return false;
}

// --------------------------------------------------------------------------------------- items
static void add_item(int list, const char *text) {
    if (!text[0]) return;
    load();
    if (store.count[list] >= MAX_ITEMS) {
        memmove(store.items[list][0], store.items[list][1], (MAX_ITEMS - 1) * ITEM_BYTES);
        store.count[list] = MAX_ITEMS - 1;
    }
    char *slot = store.items[list][store.count[list]];
    bool truncated = strlen(text) >= ITEM_BYTES;
    strlcpy(slot, text, ITEM_BYTES);
    if (truncated) {  // do not leave half of a UTF-8 letter at the end
        size_t len = strlen(slot);
        while (len > 0 && ((unsigned char)slot[len - 1] & 0xC0) == 0x80) len--;
        if (len > 0 && (unsigned char)slot[len - 1] >= 0xC0) len--;
        slot[len] = '\0';
    }
    store.count[list]++;
}

static bool already_there(int list, const char *plain) {
    for (int i = 0; i < store.count[list]; i++) {
        char folded[ITEM_BYTES];
        fold(store.items[list][i], folded, sizeof(folded));
        if (!strcmp(folded, plain)) return true;
    }
    return false;
}

static char *build_json(int list) {
    char *json = heap_caps_malloc(900, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    size_t used = snprintf(json, 900, "{\"type\":\"rows\",\"title\":\"%s\",\"rows\":[", list == 0 ? "Lista de compras" : "Notas");
    int shown = 0;
    for (int i = store.count[list] - 1; i >= 0 && shown < 5; i--, shown++) {
        char label[ITEM_BYTES];
        strlcpy(label, store.items[list][i], sizeof(label));
        for (char *c = label; *c; c++) if (*c == '"' || *c == '\\') *c = '\'';
        used += snprintf(json + used, 900 - used, "%s{\"label\":\"%s\",\"note\":\"\",\"value\":\"\",\"change\":\"\",\"trend\":\"flat\"}",
                         shown ? "," : "", label);
    }
    snprintf(json + used, 900 - used, "],\"footer\":\"%s\"}", list == 0 ? "Dime: agrega algo a la lista" : "Dime: anota que...");
    return json;
}

static void show(local_answer_t *a, int list) {
    a->json = build_json(list);
    strlcpy(a->screen, a->json ? "rows" : "", sizeof(a->screen));
}

// ------------------------------------------------------------------------------------ the skill
bool local_lists_try(const char *original, local_answer_t *a) {
    words_t w;
    split_words(original, &w);
    if (w.n == 0) return false;

    static const char *const add_verbs[] = {"agrega", "agregar", "agregame", "anade", "anadir", "anademe", "pon", "ponme",
                                            "poner", "mete", "apunta", "apuntame", "anota", "anotame", "incluye", NULL};
    static const char *const remove_verbs[] = {"quita", "quitar", "saca", "sacar", "borra", "borrar", "elimina", "eliminar", "tacha", "vacia", "vaciar", "limpia", "limpiar", NULL};
    static const char *const show_verbs[] = {"que", "muestra", "muestrame", "lee", "leeme", "dime", "abre", "ver", "cual", "cuales", NULL};
    static const char *const note_verbs[] = {"anota", "anotame", "apunta", "apuntame", NULL};
    static const char *const stop_after[] = {"a", "en", "de", "para", NULL};
    static const char *const filler[] = {"la", "mi", "el", "mis", "un", "una", "unos", "unas", "por", "favor", "hola", "bug", "a", "al", "en", "y", NULL};

    bool list_word = has_word(&w, "lista") || has_word(&w, "compras") || has_word(&w, "mercado") || has_word(&w, "supermercado") ||
                     has_word(&w, "shopping");
    bool notes_word = has_word(&w, "notas") || has_word(&w, "nota") || has_word(&w, "notes");
    // Just the name of the list ("lista de compras", "mis notas", "shopping list") shows it
    bool bare_name = (list_word || notes_word) && w.n <= 4 && !has_any(&w, add_verbs) && !has_any(&w, remove_verbs);

    // ---- clear or remove ("borra la lista", "quita la leche de la lista", "borra las notas")
    if (has_any(&w, remove_verbs) && (list_word || notes_word)) {
        int list = (notes_word && !list_word) ? 1 : 0;
        // "quita X de la lista": X is what is between the verb and "de"/"a la lista"
        int verb = -1, stop = w.n;
        for (int i = 0; i < w.n && verb < 0; i++) if (is_one_of(w.plain[i], remove_verbs)) verb = i;
        for (int i = verb + 1; i + 1 < w.n; i++) {
            if (is_one_of(w.plain[i], stop_after) && (!strcmp(w.plain[i + 1], "la") || !strcmp(w.plain[i + 1], "mi"))) { stop = i; break; }
        }
        char what[ITEM_BYTES] = "";
        for (int i = verb + 1; i < stop; i++) {
            static const char *const skip_when_removing[] = {"lista", "notas", "nota", "compras", "mercado", "supermercado", "de", "del", "todo", "toda", "todas", "todos", "las", "los", NULL};
            if (is_one_of(w.plain[i], filler) || is_one_of(w.plain[i], skip_when_removing)) continue;
            if (what[0]) strlcat(what, " ", sizeof(what));
            strlcat(what, w.plain[i], sizeof(what));
        }
        load();
        if (!what[0]) {  // the whole list
            store.count[list] = 0;
            memset(store.items[list], 0, sizeof(store.items[list]));
            save();
            ls_say(a, list == 0 ? CLIP_BORRE_LISTA : CLIP_BORRE_NOTAS);
            ls_finish(a, list == 0 ? "Listo, borré la lista de compras." : "Listo, borré las notas.", NULL);
            return true;
        }
        int kept = 0;
        for (int i = 0; i < store.count[list]; i++) {
            char folded[ITEM_BYTES];
            fold(store.items[list][i], folded, sizeof(folded));
            char squeezed[ITEM_BYTES];
            size_t o = 0;
            for (const char *c = what; *c && o + 1 < sizeof(squeezed); c++) if (*c != ' ') squeezed[o++] = *c;
            squeezed[o] = '\0';
            if (!strstr(folded, squeezed)) {
                if (kept != i) memcpy(store.items[list][kept], store.items[list][i], ITEM_BYTES);
                kept++;
            }
        }
        if (kept == store.count[list]) {
            ls_say(a, CLIP_NO_ENCONTRE_LISTA);
            ls_finish(a, "No encontré eso en tu lista.", NULL);
        } else {
            for (int i = kept; i < store.count[list]; i++) store.items[list][i][0] = '\0';
            store.count[list] = (uint8_t)kept;
            save();
            ls_say(a, CLIP_QUITE_ESO);
            ls_finish(a, "Listo, quité eso de la lista.", NULL);
            show(a, list);
        }
        return true;
    }

    // ---- show ("qué hay en mi lista", "mis notas")
    if ((list_word || notes_word) && (bare_name || has_any(&w, show_verbs) || (notes_word && has_word(&w, "mis")) || has_word(&w, "mi")) &&
        !has_any(&w, add_verbs)) {
        int list = (notes_word && !list_word) ? 1 : 0;
        load();
        int count = store.count[list];
        if (count == 0) {
            ls_say(a, list == 0 ? CLIP_LISTA_VACIA : CLIP_NO_NOTAS);
            ls_finish(a, list == 0 ? "Tu lista de compras está vacía." : "No tienes notas.", NULL);
        } else {
            if (count == 1) {
                ls_say(a, list == 0 ? CLIP_UNA_COSA_LISTA : CLIP_UNA_NOTA);
            } else {
                ls_say(a, CLIP_TIENES);
                ls_say_number(a, count);
                ls_say(a, list == 0 ? CLIP_COSAS_EN_LISTA : CLIP_NOTAS_N);
            }
            char reply[64];
            snprintf(reply, sizeof(reply), count == 1 ? (list == 0 ? "Tienes una cosa en la lista de compras." : "Tienes una nota.")
                                                      : (list == 0 ? "Tienes %d cosas en la lista de compras." : "Tienes %d notas."), count);
            ls_finish(a, reply, NULL);
            show(a, list);
        }
        return true;
    }

    // ---- a note: "anota que mañana hay reunión"
    int que = -1;
    for (int i = 0; i + 1 < w.n; i++) {
        if (is_one_of(w.plain[i], note_verbs) && !list_word) {
            for (int j = i + 1; j < w.n; j++) if (!strcmp(w.plain[j], "que")) { que = j; break; }
            break;
        }
    }
    bool note_request = que >= 0 || (has_word(&w, "toma") && has_word(&w, "nota")) || (has_word(&w, "nueva") && notes_word);
    if (note_request && !list_word) {
        int start = que >= 0 ? que + 1 : (has_word(&w, "nota") ? 0 : 0);
        if (que < 0) {  // "toma nota ..." / "nueva nota ...": what follows the word "nota"
            for (int i = 0; i < w.n; i++) if (!strcmp(w.plain[i], "nota")) start = i + 1;
        }
        char note[ITEM_BYTES] = "";
        for (int i = start; i < w.n; i++) {
            if (note[0]) strlcat(note, " ", sizeof(note));
            strlcat(note, w.original[i], sizeof(note));
        }
        if (!note[0]) {
            ls_say(a, CLIP_QUE_ANOTO);
            ls_finish(a, "¿Qué anoto? Por ejemplo: anota que debo llamar a Ana.", NULL);
            return true;
        }
        load();
        add_item(1, note);
        save();
        ls_say(a, CLIP_LISTO_ANOTE);
        ls_finish(a, "Listo, lo anoté.", NULL);
        show(a, 1);
        return true;
    }

    // ---- add to the shopping list: "agrega leche y huevos a la lista de compras"
    if (list_word && has_any(&w, add_verbs)) {
        int verb = 0;
        for (int i = 0; i < w.n; i++) if (is_one_of(w.plain[i], add_verbs)) { verb = i; break; }
        int stop = w.n;
        for (int i = verb + 1; i < w.n; i++) {
            if (is_one_of(w.plain[i], stop_after)) {
                int k = i + 1;
                if (k < w.n && (!strcmp(w.plain[k], "la") || !strcmp(w.plain[k], "mi") || !strcmp(w.plain[k], "el"))) k++;
                if (k < w.n && (!strcmp(w.plain[k], "lista") || !strcmp(w.plain[k], "compras") || !strcmp(w.plain[k], "mercado") ||
                                !strcmp(w.plain[k], "supermercado"))) { stop = i; break; }
            }
        }
        load();
        char current[ITEM_BYTES] = "";
        int added = 0, existing = 0;
        for (int i = verb + 1; i <= stop; i++) {
            bool separator = i == stop || !strcmp(w.plain[i], "y") || !strcmp(w.plain[i], "e") || (i > 0 && w.comma_after[i - 1]);
            if (separator && current[0]) {
                char folded[ITEM_BYTES];
                fold(current, folded, sizeof(folded));
                if (already_there(0, folded)) existing++;
                else { add_item(0, current); added++; }
                current[0] = '\0';
            }
            if (i == stop || !strcmp(w.plain[i], "y") || !strcmp(w.plain[i], "e")) continue;
            if (!current[0] && is_one_of(w.plain[i], filler)) continue;  // "agrega la leche": skip the article
            if (current[0]) strlcat(current, " ", sizeof(current));
            strlcat(current, w.original[i], sizeof(current));
        }
        if (!added && !existing) {
            ls_say(a, CLIP_QUE_AGREGO);
            ls_finish(a, "¿Qué quieres agregar? Por ejemplo: agrega leche a la lista de compras.", NULL);
            return true;
        }
        if (added) save();
        ls_say(a, added ? CLIP_LISTO_AGREGUE : CLIP_YA_ESTABA);
        ls_finish(a, added ? "Listo, lo agregué a tu lista de compras." : "Eso ya estaba en tu lista de compras.", NULL);
        show(a, 0);
        return true;
    }
    return false;
}

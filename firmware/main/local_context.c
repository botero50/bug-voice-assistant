// Follow-up questions that lean on the last exchange: "y mañana?", "y en Bogotá?", "y el euro?", "y Shakira?".
// The short sentence is rewritten into a complete one, which then goes through the normal flow (a skill or the PC).
#include "local_context.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Lowercase, no accents, one space between words, spaces at both ends
static void norm(const char *in, char *out, size_t size) {
    size_t o = 0;
    out[o++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 2 < size; p++) {
        char c;
        if (*p == 0xC3 && p[1]) {
            switch (p[1]) {
            case 0xA1: case 0x81: c = 'a'; break;
            case 0xA9: case 0x89: c = 'e'; break;
            case 0xAD: case 0x8D: c = 'i'; break;
            case 0xB3: case 0x93: c = 'o'; break;
            case 0xBA: case 0x9A: case 0xBC: case 0x9C: c = 'u'; break;
            case 0xB1: case 0x91: c = 'n'; break;
            default: c = ' ';
            }
            p++;
        } else {
            c = isalnum(*p) ? (char)tolower(*p) : ' ';
        }
        if (c == ' ' && out[o - 1] == ' ') continue;
        out[o++] = c;
    }
    if (out[o - 1] != ' ') out[o++] = ' ';
    out[o] = '\0';
}

static bool has_any(const char *text, const char *const *words) {
    char padded[48];
    for (; *words; words++) {
        snprintf(padded, sizeof(padded), " %s ", *words);
        if (strstr(text, padded)) return true;
    }
    return false;
}

// The text after "y " (or "y en ", "y el ", "y la "), as it was heard; empty if the sentence does not start with "y"
static const char *after_and(const char *original) {
    const char *p = original;
    while (*p && !isalnum((unsigned char)*p) && (unsigned char)*p < 0x80) p++;
    if ((p[0] == 'y' || p[0] == 'Y') && (p[1] == ' ' || p[1] == ',')) {
        p++;
        while (*p == ' ' || *p == ',') p++;
        return p;
    }
    return NULL;
}

bool local_context_is_repeat(const char *original) {
    char t[160];
    norm(original, t, sizeof(t));
    static const char *const phrases[] = {"repite", "repitelo", "repite eso", "repite lo ultimo", "repite lo que dijiste", "que dijiste", "que dijiste otra vez",
                                          "otra vez", "dilo otra vez", "no te escuche", "no entendi", "como", "repetir", "puedes repetir", "me lo repites", NULL};
    // the whole sentence must be one of them (a "como" inside "cómo está el clima" is not a request to repeat)
    char padded[48];
    for (const char *const *w = phrases; *w; w++) {
        snprintf(padded, sizeof(padded), " %s ", *w);
        if (strcmp(t, padded) == 0) return true;
    }
    return strncmp(t, " repite", 7) == 0;
}

bool local_context_expand(const char *original, const char *last_heard, char *out, size_t size) {
    const char *rest = after_and(original);
    if (!rest || !*rest || !last_heard || !*last_heard) return false;
    char t[160], last[160], r[160];
    norm(original, t, sizeof(t));
    norm(last_heard, last, sizeof(last));
    norm(rest, r, sizeof(r));
    if (strlen(r) > 60) return false;  // a long sentence is a new question, not a follow-up

    static const char *const weather[] = {"clima", "tiempo", "temperatura", "llover", "llueve", "lluvia", "calor", "frio", NULL};
    static const char *const price[] = {"dolar", "dolares", "euro", "euros", "bitcoin", "precio", "cuesta", "vale", "cotiza", "cuanto", "peso", "pesos", NULL};
    static const char *const who[] = {"quien", "quienes", "que", "cantante", "actor", "famoso", "wikipedia", "significa", NULL};
    static const char *const days[] = {"hoy", "manana", "pasado", "lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo", "noche", "tarde", NULL};

    // "y mañana", "y el viernes": another day of the weather
    if (has_any(last, weather)) {
        if (has_any(r, days) || strstr(r, " manana ")) {
            snprintf(out, size, "cómo estará el clima %s", rest);
            return true;
        }
        // "y en Bogotá": the same weather somewhere else
        if (strncmp(r, " en ", 4) == 0) {
            snprintf(out, size, "cómo está el clima %s", rest);
            return true;
        }
        snprintf(out, size, "cómo está el clima en %s", rest);
        return true;
    }
    // "y el euro", "y el bitcoin": another price
    if (has_any(last, price)) {
        if (has_any(r, days)) return false;  // "y mañana" is not a currency
        if (strncmp(r, " el ", 4) == 0 || strncmp(r, " la ", 4) == 0 || strncmp(r, " los ", 5) == 0) {
            snprintf(out, size, "cuánto vale %s", rest);
        } else {
            snprintf(out, size, "cuánto vale el %s", rest);
        }
        return true;
    }
    // "y Shakira": another person or thing, asked the same way
    if (has_any(last, who)) {
        const char *head = strstr(last, " que es ") ? "qué es" : "quién es";
        snprintf(out, size, "%s %s", head, rest);
        return true;
    }
    (void)t;
    return false;
}

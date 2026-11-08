#include "local_skills_internal.h"
#include "voice_clips.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Arithmetic ("cuánto es doce por cinco") and unit conversions ("cuántos kilómetros son diez millas"), done by the
// board and answered with its recorded words. `text` is normalized: lowercase, no accents, no punctuation.

#define MAX_TOKENS 24
#define BIG_LIMIT 999999999LL

typedef struct {
    char words[200];
    const char *tok[MAX_TOKENS];
    int n;
} tokens_t;

static void tokenize(tokens_t *t, const char *text) {
    strlcpy(t->words, text, sizeof(t->words));
    t->n = 0;
    char *save = NULL;
    for (char *w = strtok_r(t->words, " ", &save); w && t->n < MAX_TOKENS; w = strtok_r(NULL, " ", &save)) t->tok[t->n++] = w;
}

static bool is(const tokens_t *t, int i, const char *word) {
    return i >= 0 && i < t->n && strcmp(t->tok[i], word) == 0;
}

// ---------------------------------------------------------------------------- saying big numbers
static void say_below_1000(local_answer_t *a, int n) {
    if (n >= 100) {
        if (n == 100) {
            ls_say(a, CLIP_T100);
            return;
        }
        ls_say(a, n < 200 ? CLIP_CIENTO : CLIP_H200 + (n / 100 - 2));
        n %= 100;
    }
    if (n > 0) ls_say_number(a, n);
}

void ls_say_big(local_answer_t *a, long long n) {
    if (n < 0) n = 0;
    if (n > BIG_LIMIT) n = BIG_LIMIT;
    int millions = (int)(n / 1000000), thousands = (int)((n / 1000) % 1000), rest = (int)(n % 1000);
    if (millions) {
        if (millions == 1) {
            ls_say(a, CLIP_UN_MILLON);
        } else {
            say_below_1000(a, millions);
            ls_say(a, CLIP_MILLONES);
        }
    }
    if (thousands) {
        if (thousands != 1) say_below_1000(a, thousands);
        ls_say(a, CLIP_MIL);
    }
    if (rest) {
        say_below_1000(a, rest);
    } else if (n == 0) {
        ls_say(a, CLIP_N0);
    }
}

// 1234567 -> "1.234.567"
static void format_int(long long n, char *out, size_t size) {
    char plain[24];
    snprintf(plain, sizeof(plain), "%lld", n < 0 ? -n : n);
    size_t len = strlen(plain), o = 0;
    if (n < 0 && o + 1 < size) out[o++] = '-';
    for (size_t i = 0; i < len && o + 2 < size; i++) {
        if (i > 0 && (len - i) % 3 == 0) out[o++] = '.';
        out[o++] = plain[i];
    }
    out[o] = '\0';
}

// A result with at most one decimal, spoken and written
static bool say_value(local_answer_t *a, double value, char *text, size_t size) {
    long long tenths = llround(fabs(value) * 10.0);
    if (tenths > BIG_LIMIT * 10) return false;
    bool negative = value < 0 && tenths > 0;
    long long whole = tenths / 10;
    int decimal = (int)(tenths % 10);
    if (negative) ls_say(a, CLIP_MENOS);
    ls_say_big(a, whole);
    char digits[32];
    format_int(whole, digits, sizeof(digits));
    if (decimal) {
        ls_say(a, CLIP_COMA);
        ls_say(a, CLIP_N0 + decimal);
        snprintf(text, size, "%s%s,%d", negative ? "-" : "", digits, decimal);
    } else {
        snprintf(text, size, "%s%s", negative ? "-" : "", digits);
    }
    return true;
}

// ------------------------------------------------------------------------------- reading numbers
static const struct { const char *word; int value; } NUMBER_WORDS[] = {
    {"cero", 0}, {"un", 1}, {"uno", 1}, {"una", 1}, {"dos", 2}, {"tres", 3}, {"cuatro", 4}, {"cinco", 5}, {"seis", 6},
    {"siete", 7}, {"ocho", 8}, {"nueve", 9}, {"diez", 10}, {"once", 11}, {"doce", 12}, {"trece", 13}, {"catorce", 14},
    {"quince", 15}, {"dieciseis", 16}, {"diecisiete", 17}, {"dieciocho", 18}, {"diecinueve", 19}, {"veinte", 20},
    {"veintiuno", 21}, {"veintiun", 21}, {"veintidos", 22}, {"veintitres", 23}, {"veinticuatro", 24}, {"veinticinco", 25},
    {"veintiseis", 26}, {"veintisiete", 27}, {"veintiocho", 28}, {"veintinueve", 29}, {"treinta", 30}, {"cuarenta", 40},
    {"cincuenta", 50}, {"sesenta", 60}, {"setenta", 70}, {"ochenta", 80}, {"noventa", 90}, {"cien", 100}, {"ciento", 100},
    {"doscientos", 200}, {"trescientos", 300}, {"cuatrocientos", 400}, {"quinientos", 500}, {"seiscientos", 600},
    {"setecientos", 700}, {"ochocientos", 800}, {"novecientos", 900}};

static int word_value(const char *w) {
    for (size_t i = 0; i < sizeof(NUMBER_WORDS) / sizeof(NUMBER_WORDS[0]); i++) {
        if (strcmp(w, NUMBER_WORDS[i].word) == 0) return NUMBER_WORDS[i].value;
    }
    return -1;
}

// A number starting at token i: "12", "doce", "treinta y cinco", "dos mil quinientos". Returns false if there is none.
static bool number_at(const tokens_t *t, int i, int *used, long long *out) {
    long long total = 0, current = 0;
    int j = i;
    bool found = false, last_digit = false;
    while (j < t->n) {
        const char *w = t->tok[j];
        int v;
        if (isdigit((unsigned char)w[0])) {
            if (found && (last_digit || current)) break;
            current = atoll(w);
            last_digit = true;
        } else if (strcmp(w, "mil") == 0) {
            total += (current ? current : 1) * 1000;
            current = 0;
            last_digit = false;
        } else if (strcmp(w, "millon") == 0 || strcmp(w, "millones") == 0) {
            total += (current ? current : 1) * 1000000;
            current = 0;
            last_digit = false;
        } else if (strcmp(w, "y") == 0) {
            if (!found || j + 1 >= t->n) break;
            int next = word_value(t->tok[j + 1]);
            if (next < 1 || next > 9) break;
            j++;
            continue;
        } else if ((v = word_value(w)) >= 0) {
            if (found && !last_digit && current && current % 10 != 0 && v < 10) break;  // "dos tres" is two numbers
            current += v;
            last_digit = false;
        } else {
            break;
        }
        found = true;
        j++;
    }
    if (!found) return false;
    *used = j - i;
    *out = total + current;
    return true;
}

// ------------------------------------------------------------------------------------- arithmetic
static bool try_arithmetic(const tokens_t *t, local_answer_t *a) {
    bool asked = false;
    for (int i = 0; i < t->n; i++) {
        const char *w = t->tok[i];
        if (strncmp(w, "cuanto", 6) == 0 || strncmp(w, "calcul", 6) == 0 || strncmp(w, "resuelv", 7) == 0 ||
            strcmp(w, "suma") == 0 || strcmp(w, "resta") == 0 || strcmp(w, "multiplica") == 0 || strcmp(w, "divide") == 0) {
            asked = true;
        }
    }
    if (!asked) return false;

    for (int i = 0; i < t->n; i++) {
        int used1;
        long long first;
        if (!number_at(t, i, &used1, &first)) continue;
        int k = i + used1;
        if (k >= t->n) return false;
        const char *op = t->tok[k];
        char kind = 0;
        if (strcmp(op, "mas") == 0) kind = '+';
        else if (strcmp(op, "menos") == 0) kind = '-';
        else if (strcmp(op, "por") == 0 || strcmp(op, "x") == 0 || strcmp(op, "multiplicado") == 0 || strcmp(op, "veces") == 0) kind = '*';
        else if (strcmp(op, "dividido") == 0 || strcmp(op, "entre") == 0 || strcmp(op, "sobre") == 0) kind = '/';
        if (!kind) return false;
        k++;
        if (k < t->n && (is(t, k, "por") || is(t, k, "entre") || is(t, k, "a"))) k++;
        int used2;
        long long second;
        if (!number_at(t, k, &used2, &second)) return false;

        double value = 0;
        if (kind == '+') value = (double)first + (double)second;
        else if (kind == '-') value = (double)first - (double)second;
        else if (kind == '*') value = (double)first * (double)second;
        else {
            if (second == 0) {
                ls_say(a, CLIP_NO_DIVIDIR);
                ls_finish(a, "No se puede dividir entre cero.", NULL);
                return true;
            }
            value = (double)first / (double)second;
        }
        char shown[40];
        ls_say(a, CLIP_EL_RESULTADO_ES);
        if (!say_value(a, value, shown, sizeof(shown))) {
            memset(a, 0, sizeof(*a));
            return false;
        }
        char reply[96];
        snprintf(reply, sizeof(reply), "El resultado es %s.", shown);
        ls_finish(a, reply, NULL);
        return true;
    }
    return false;
}

// -------------------------------------------------------------------------------------- conversions
typedef struct {
    const char *word;
    int group;       // only units of the same group convert
    double to_base;  // multiply to get the group's base unit (temperatures are handled apart)
    int clip;
    const char *name;
} unit_t;

enum { G_TEMP, G_LENGTH, G_MASS, G_VOLUME, G_SMALL };

static const unit_t UNITS[] = {
    {"celsius", G_TEMP, 0, CLIP_U_CELSIUS, "grados Celsius"},
    {"centigrados", G_TEMP, 0, CLIP_U_CELSIUS, "grados Celsius"},
    {"fahrenheit", G_TEMP, 1, CLIP_U_FAHRENHEIT, "grados Fahrenheit"},
    {"kilometro", G_LENGTH, 1000.0, CLIP_U_KM, "kilómetros"},
    {"kilometros", G_LENGTH, 1000.0, CLIP_U_KM, "kilómetros"},
    {"milla", G_LENGTH, 1609.344, CLIP_U_MILLAS, "millas"},
    {"millas", G_LENGTH, 1609.344, CLIP_U_MILLAS, "millas"},
    {"metro", G_LENGTH, 1.0, CLIP_U_METROS, "metros"},
    {"metros", G_LENGTH, 1.0, CLIP_U_METROS, "metros"},
    {"pie", G_LENGTH, 0.3048, CLIP_U_PIES, "pies"},
    {"pies", G_LENGTH, 0.3048, CLIP_U_PIES, "pies"},
    {"kilo", G_MASS, 1.0, CLIP_U_KILOS, "kilos"},
    {"kilos", G_MASS, 1.0, CLIP_U_KILOS, "kilos"},
    {"kilogramo", G_MASS, 1.0, CLIP_U_KILOS, "kilos"},
    {"kilogramos", G_MASS, 1.0, CLIP_U_KILOS, "kilos"},
    {"libra", G_MASS, 0.45359237, CLIP_U_LIBRAS, "libras"},
    {"libras", G_MASS, 0.45359237, CLIP_U_LIBRAS, "libras"},
    {"litro", G_VOLUME, 1.0, CLIP_U_LITROS, "litros"},
    {"litros", G_VOLUME, 1.0, CLIP_U_LITROS, "litros"},
    {"galon", G_VOLUME, 3.785411784, CLIP_U_GALONES, "galones"},
    {"galones", G_VOLUME, 3.785411784, CLIP_U_GALONES, "galones"},
    {"centimetro", G_SMALL, 1.0, CLIP_U_CM, "centímetros"},
    {"centimetros", G_SMALL, 1.0, CLIP_U_CM, "centímetros"},
    {"pulgada", G_SMALL, 2.54, CLIP_U_PULGADAS, "pulgadas"},
    {"pulgadas", G_SMALL, 2.54, CLIP_U_PULGADAS, "pulgadas"},
};

static const unit_t *find_unit(const char *w) {
    for (size_t i = 0; i < sizeof(UNITS) / sizeof(UNITS[0]); i++) {
        if (strcmp(w, UNITS[i].word) == 0) return &UNITS[i];
    }
    return NULL;
}

static bool try_conversion(const tokens_t *t, local_answer_t *a) {
    for (int i = 0; i < t->n; i++) {
        int used;
        long long amount;
        if (!number_at(t, i, &used, &amount)) continue;
        int k = i + used;
        if (is(t, k, "grados") || is(t, k, "grado")) k++;
        const unit_t *from = k < t->n ? find_unit(t->tok[k]) : NULL;
        if (!from) continue;
        const unit_t *to = NULL;
        for (int j = 0; j < t->n; j++) {
            if (j == k) continue;
            const unit_t *u = find_unit(t->tok[j]);
            if (u && u != from && u->group == from->group && u->clip != from->clip) to = u;
        }
        if (!to) return false;

        double value;
        if (from->group == G_TEMP) {
            value = from->to_base == 0 ? (double)amount * 9.0 / 5.0 + 32.0 : ((double)amount - 32.0) * 5.0 / 9.0;
        } else {
            value = (double)amount * from->to_base / to->to_base;
        }
        char shown[40];
        ls_say(a, CLIP_SON_RES);
        if (!say_value(a, value, shown, sizeof(shown))) {
            memset(a, 0, sizeof(*a));
            return false;
        }
        ls_say(a, to->clip);
        char reply[96];
        snprintf(reply, sizeof(reply), "Son %s %s.", shown, to->name);
        ls_finish(a, reply, NULL);
        return true;
    }
    return false;
}

bool local_math_try(const char *text, local_answer_t *a) {
    tokens_t t;
    tokenize(&t, text);
    return try_arithmetic(&t, a) || try_conversion(&t, a);
}

#include "alarms.h"
#include "esp_heap_caps.h"
#include "local_skills_internal.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Timers, alarms and reminders, understood and kept by the board: "pon un temporizador de diez minutos",
// "despiértame a las seis y media", "todos los lunes a las 8", "cancela las alarmas", "qué alarmas tengo".
// A reminder with something to remember ("recuérdame llamar a Ana") has a label the board cannot say aloud,
// so that one still goes to the PC.

#define MAX_TOKENS 48

typedef struct {
    char words[200];
    const char *tok[MAX_TOKENS];
    int n;
} tokens_t;

static void tokenize(const char *text, tokens_t *t) {
    strlcpy(t->words, text, sizeof(t->words));
    t->n = 0;
    char *save = NULL;
    for (char *w = strtok_r(t->words, " ", &save); w && t->n < MAX_TOKENS; w = strtok_r(NULL, " ", &save)) {
        t->tok[t->n++] = w;
    }
}

static bool is(const tokens_t *t, int i, const char *word) {
    return i >= 0 && i < t->n && strcmp(t->tok[i], word) == 0;
}

static bool contains(const tokens_t *t, const char *word) {
    for (int i = 0; i < t->n; i++) {
        if (is(t, i, word)) return true;
    }
    return false;
}

// ----------------------------------------------------------------------------------- numbers
static const struct { const char *word; int value; } NUMBER_WORDS[] = {
    {"un", 1}, {"uno", 1}, {"una", 1}, {"dos", 2}, {"tres", 3}, {"cuatro", 4}, {"cinco", 5}, {"seis", 6}, {"siete", 7},
    {"ocho", 8}, {"nueve", 9}, {"diez", 10}, {"once", 11}, {"doce", 12}, {"trece", 13}, {"catorce", 14}, {"quince", 15},
    {"dieciseis", 16}, {"diecisiete", 17}, {"dieciocho", 18}, {"diecinueve", 19}, {"veinte", 20}, {"veintiuno", 21},
    {"veintidos", 22}, {"veintitres", 23}, {"veinticuatro", 24}, {"veinticinco", 25}, {"veintiseis", 26},
    {"veintisiete", 27}, {"veintiocho", 28}, {"veintinueve", 29}, {"treinta", 30}, {"cuarenta", 40}, {"cincuenta", 50},
    {"sesenta", 60}, {"cero", 0}};

static int word_value(const char *w) {
    for (size_t i = 0; i < sizeof(NUMBER_WORDS) / sizeof(NUMBER_WORDS[0]); i++) {
        if (strcmp(w, NUMBER_WORDS[i].word) == 0) return NUMBER_WORDS[i].value;
    }
    return -1;
}

// A number starting at token i ("10", "diez", "treinta y cinco"). Returns -1 if there is none; `used` is how many
// tokens it took.
static int number_at(const tokens_t *t, int i, int *used) {
    if (i < 0 || i >= t->n) return -1;
    const char *w = t->tok[i];
    if (isdigit((unsigned char)w[0])) {
        char *end;
        long v = strtol(w, &end, 10);
        if (*end != '\0') return -1;  // "6am" and the like
        *used = 1;
        return (int)v;
    }
    int v = word_value(w);
    if (v < 0) return -1;
    *used = 1;
    if (v >= 20 && v % 10 == 0 && is(t, i + 1, "y") && i + 2 < t->n) {
        int unit = word_value(t->tok[i + 2]);
        if (unit >= 1 && unit <= 9) {
            *used = 3;
            return v + unit;
        }
    }
    return v;
}

static bool unit_seconds(const char *w, int *seconds) {
    if (!strcmp(w, "hora") || !strcmp(w, "horas") || !strcmp(w, "hr") || !strcmp(w, "hrs")) { *seconds = 3600; return true; }
    if (!strcmp(w, "minuto") || !strcmp(w, "minutos") || !strcmp(w, "min") || !strcmp(w, "mins")) { *seconds = 60; return true; }
    if (!strcmp(w, "segundo") || !strcmp(w, "segundos") || !strcmp(w, "seg") || !strcmp(w, "segs")) { *seconds = 1; return true; }
    return false;
}

// ----------------------------------------------------------------------------------- durations
// "diez minutos", "2 horas y media", "media hora", "una hora 30 minutos". `mask` marks the tokens understood.
static int parse_duration(const tokens_t *t, bool *mask) {
    int total = 0;
    for (int i = 0; i < t->n; i++) {
        if (is(t, i, "media") && is(t, i + 1, "hora")) {
            total += 1800;
            mask[i] = mask[i + 1] = true;
            i++;
        } else if (is(t, i, "cuarto") && is(t, i + 1, "de") && is(t, i + 2, "hora")) {
            total += 900;
            mask[i] = mask[i + 1] = mask[i + 2] = true;
            i += 2;
        } else {
            int used = 0, seconds = 0;
            int number = number_at(t, i, &used);
            if (number >= 0 && i + used < t->n && unit_seconds(t->tok[i + used], &seconds)) {
                total += number * seconds;
                for (int k = 0; k <= used; k++) mask[i + k] = true;
                int next = i + used + 1;
                if (seconds == 3600 && is(t, next, "y") && is(t, next + 1, "media")) {
                    total += 1800;
                    mask[next] = mask[next + 1] = true;
                    next += 2;
                }
                i = next - 1;
            }
        }
    }
    return total;
}

// ---------------------------------------------------------------------------------- clock times
typedef struct {
    bool found;
    int hour, minute;          // 0-23, 0-59
    bool tomorrow;
    bool ambiguous;            // no am/pm said: hour is the morning one, the afternoon one is hour + 12
} clock_time_t;

static int weekday_index(const char *w) {  // Monday = 0, or -1
    static const char *const names[] = {"lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo"};
    for (int i = 0; i < 7; i++) {
        if (!strcmp(w, names[i])) return i;
    }
    return -1;
}

// "a las 6 y media de la manana": the hour and minute, and whether "manana" (tomorrow) was said
static clock_time_t parse_clock(const tokens_t *t, bool *mask) {
    clock_time_t c = {0};
    for (int i = 0; i + 2 < t->n + 0; i++) {
        if (!(is(t, i, "a") && (is(t, i + 1, "las") || is(t, i + 1, "la")))) continue;
        int used = 0;
        int hour = number_at(t, i + 2, &used);
        if (hour < 0 || hour > 24) continue;
        int next = i + 2 + used;
        int minute = 0;
        int start_hour = hour;
        if (is(t, next, "y") && is(t, next + 1, "media")) {
            minute = 30;
            next += 2;
        } else if (is(t, next, "y") && is(t, next + 1, "cuarto")) {
            minute = 15;
            next += 2;
        } else if (is(t, next, "menos") && is(t, next + 1, "cuarto")) {
            hour = (hour + 23) % 24;
            minute = 45;
            next += 2;
        } else if (is(t, next, "en") && is(t, next + 1, "punto")) {
            next += 2;
        } else if (is(t, next, "y")) {
            int u2 = 0;
            int m = number_at(t, next + 1, &u2);
            if (m >= 0 && m < 60) {
                minute = m;
                next += 1 + u2;
            }
        } else {
            int u2 = 0;
            int m = number_at(t, next, &u2);
            int dummy;
            if (m >= 0 && m < 60 && isdigit((unsigned char)t->tok[next][0]) &&
                !(next + u2 < t->n && unit_seconds(t->tok[next + u2], &dummy))) {
                minute = m;
                next += u2;
            }
        }
        // am / pm / de la manana ...
        int period = -1;  // 0 morning, 1 afternoon/evening, 2 noon
        if (is(t, next, "de") && is(t, next + 1, "la") && (is(t, next + 2, "manana") || is(t, next + 2, "madrugada"))) {
            period = 0; next += 3;
        } else if (is(t, next, "de") && is(t, next + 1, "la") && (is(t, next + 2, "tarde") || is(t, next + 2, "noche"))) {
            period = 1; next += 3;
        } else if (is(t, next, "del") && is(t, next + 1, "mediodia")) {
            period = 2; next += 2;
        } else if (is(t, next, "a") && is(t, next + 1, "m")) {
            period = 0; next += 2;
        } else if (is(t, next, "p") && is(t, next + 1, "m")) {
            period = 1; next += 2;
        } else if (is(t, next, "am")) {
            period = 0; next += 1;
        } else if (is(t, next, "pm")) {
            period = 1; next += 1;
        }
        int h = hour;
        if (period == 0) h = hour % 12;
        else if (period == 1) h = hour % 12 + 12;
        else if (period == 2) h = 12;
        else if (start_hour == 0 || start_hour > 12) h = hour % 24;
        else h = -1;  // no am/pm said: the next time that happens (decided when the time is computed)
        c.found = true;
        c.ambiguous = h < 0;
        c.hour = h < 0 ? hour % 12 : h;
        c.minute = minute;
        for (int k = i; k < next && k < t->n; k++) mask[k] = true;
        break;
    }
    // "manana" meaning tomorrow, not the part of the day ("de la manana")
    for (int i = 0; i < t->n; i++) {
        if (is(t, i, "manana") && !(i >= 1 && is(t, i - 1, "la"))) {
            c.tomorrow = true;
            mask[i] = true;
        }
    }
    return c;
}

// ------------------------------------------------------------------------------------ repeats
// Returns the seconds between rings (86400 or 604800), 0 for none; weekday (Monday = 0) or -1
static uint32_t parse_repeat(const tokens_t *t, bool *mask, int *weekday) {
    *weekday = -1;
    for (int i = 0; i < t->n; i++) {
        bool every = is(t, i, "todos") || is(t, i, "todas") || is(t, i, "cada");
        if (!every) continue;
        int j = i + 1;
        if (is(t, i, "todos") || is(t, i, "todas")) {
            if (!(is(t, j, "los") || is(t, j, "las"))) continue;
            j++;
        }
        if (j >= t->n) continue;
        int wd = weekday_index(t->tok[j]);
        if (!strcmp(t->tok[j], "dias") || !strcmp(t->tok[j], "dia")) {
            for (int k = i; k <= j; k++) mask[k] = true;
            return 86400;
        }
        if (wd >= 0) {
            *weekday = wd;
            for (int k = i; k <= j; k++) mask[k] = true;
            return 604800;
        }
        if (!strcmp(t->tok[j], "semana") || !strcmp(t->tok[j], "semanas")) {
            for (int k = i; k <= j; k++) mask[k] = true;
            return 604800;
        }
    }
    if (contains(t, "diariamente")) {
        for (int i = 0; i < t->n; i++) if (is(t, i, "diariamente")) mask[i] = true;
        return 86400;
    }
    return 0;
}

// -------------------------------------------------------------------------------- speaking helpers
static void say_duration(local_answer_t *a, char *text, size_t size, int seconds) {
    int hours = seconds / 3600, minutes = seconds % 3600 / 60, secs = seconds % 60;
    size_t used = strlen(text);
    if (hours) {
        ls_say_number(a, hours);
        ls_say(a, hours == 1 ? CLIP_HORA : CLIP_HORAS);
        used += snprintf(text + used, size - used, "%d hora%s", hours, hours == 1 ? "" : "s");
    }
    if (minutes) {
        ls_say_number(a, minutes);
        ls_say(a, minutes == 1 ? CLIP_MINUTO : CLIP_MINUTOS);
        used += snprintf(text + used, size - used, "%s%d minuto%s", hours ? " y " : "", minutes, minutes == 1 ? "" : "s");
    }
    if (secs && !hours) {
        ls_say_number(a, secs);
        ls_say(a, secs == 1 ? CLIP_SEGUNDO : CLIP_SEGUNDOS);
        snprintf(text + used, size - used, "%s%d segundo%s", minutes ? " y " : "", secs, secs == 1 ? "" : "s");
    }
}

static const char *const DAY_NAMES[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};

// "a las 6 y media de la mañana" as clips and text
static void say_clock(local_answer_t *a, char *text, size_t size, int hour, int minute) {
    int hour12 = hour % 12 == 0 ? 12 : hour % 12;
    const char *period = hour < 6 ? "de la madrugada" : hour < 12 ? "de la mañana" : hour < 19 ? "de la tarde" : "de la noche";
    ls_say(a, hour12 == 1 ? CLIP_A_LA : CLIP_A_LAS);
    if (hour12 == 1) ls_say(a, CLIP_UNA);
    else ls_say_number(a, hour12);
    size_t used = strlen(text);
    used += snprintf(text + used, size - used, "%s %d", hour12 == 1 ? "a la" : "a las", hour12);
    if (minute == 15) {
        ls_say(a, CLIP_Y_CUARTO);
        used += snprintf(text + used, size - used, " y cuarto");
    } else if (minute == 30) {
        ls_say(a, CLIP_Y_MEDIA);
        used += snprintf(text + used, size - used, " y media");
    } else if (minute != 0) {
        ls_say(a, CLIP_Y);
        ls_say_number(a, minute);
        used += snprintf(text + used, size - used, " y %d", minute);
    }
    ls_say(a, hour < 6 ? CLIP_DE_LA_MADRUGADA : hour < 12 ? CLIP_DE_LA_MANANA : hour < 19 ? CLIP_DE_LA_TARDE : CLIP_DE_LA_NOCHE);
    snprintf(text + used, size - used, " %s", period);
}

// ---------------------------------------------------------------------------------- the list screen
static void remaining_text(long seconds, char *out, size_t size) {
    if (seconds >= 3600) snprintf(out, size, "%ld h %ld min", seconds / 3600, seconds % 3600 / 60);
    else if (seconds >= 60) snprintf(out, size, "%ld min", (seconds + 59) / 60);
    else snprintf(out, size, "%ld s", seconds < 0 ? 0 : seconds);
}

static char *build_list_json(void) {
    char *json = heap_caps_malloc(1400, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    size_t used = snprintf(json, 1400, "{\"type\":\"rows\",\"title\":\"Temporizadores y alarmas\",\"rows\":[");
    time_t now = time(NULL);
    int shown = 0;
    for (int i = 0; i < 4; i++) {
        alarm_entry_t e;
        if (!alarms_get_sorted(i, &e)) break;
        struct tm local;
        localtime_r(&e.at, &local);
        char label[48], left[24];
        strlcpy(label, e.label, sizeof(label));
        for (char *c = label; *c; c++) if (*c == '"' || *c == '\\') *c = '\'';
        remaining_text((long)(e.at - now), left, sizeof(left));
        used += snprintf(json + used, 1400 - used,
                         "%s{\"label\":\"%s\",\"note\":\"%ssuena a las %d:%02d %s\",\"value\":\"%s\",\"change\":\"\",\"trend\":\"flat\"}",
                         shown ? "," : "", label, e.repeat == 86400 ? "cada día · " : e.repeat ? "cada semana · " : "",
                         local.tm_hour % 12 ? local.tm_hour % 12 : 12, local.tm_min, local.tm_hour < 12 ? "am" : "pm", left);
        shown++;
    }
    snprintf(json + used, 1400 - used, "],\"footer\":\"La placa los guarda y suena aunque el PC esté apagado\"}");
    return json;
}

// ------------------------------------------------------------------------------------- the skill
static bool only_known_words(const tokens_t *t, const bool *mask) {
    static const char *const known[] = {
        "pon", "ponme", "poner", "programa", "programame", "configura", "crea", "un", "una", "el", "la", "de", "del", "en",
        "a", "las", "los", "para", "que", "me", "te", "dentro", "temporizador", "temporizadores", "alarma", "alarmas",
        "despiertame", "despertador", "recuerdame", "recordatorio", "avisame", "cuenta", "regresiva", "por", "favor",
        "y", "con", "hola", "bug", "manana", "tarde", "noche", "madrugada", "mediodia", "am", "pm", "m", "p", "dia",
        "dias", "semana", "semanas", "hora", "horas", "minuto", "minutos", "segundo", "segundos", "media", "cuarto",
        "punto", "menos", "todos", "todas", "cada", "diariamente", "lunes", "martes", "miercoles", "jueves", "viernes",
        "sabado", "domingo", "quiero", "puedes", "podrias", "necesito", "ahora", "mi", "mis", "nuevo", "nueva",
        "cuanto", "falta", "cuales", "cuantos", "cuantas", "hay", "tengo", "tienes", "dime", "muestrame", "lista",
        "cancela", "cancelar", "quita", "quitar", "elimina", "eliminar", "borra", "borrar", "apaga", "apagar",
        "programado", "programada", "programados", "programadas", "todo", "y", NULL};
    for (int i = 0; i < t->n; i++) {
        if (mask[i]) continue;
        bool ok = isdigit((unsigned char)t->tok[i][0]) || word_value(t->tok[i]) >= 0;
        for (int k = 0; !ok && known[k]; k++) ok = !strcmp(t->tok[i], known[k]);
        if (!ok) return false;
    }
    return true;
}

bool local_timers_try(const char *text, local_answer_t *a) {
    tokens_t t;
    tokenize(text, &t);
    bool trigger = contains(&t, "temporizador") || contains(&t, "temporizadores") || contains(&t, "alarma") ||
                   contains(&t, "alarmas") || contains(&t, "despiertame") || contains(&t, "despertador") ||
                   contains(&t, "recuerdame") || contains(&t, "avisame") || contains(&t, "recordatorio") ||
                   contains(&t, "recordatorios") || (contains(&t, "cuenta") && contains(&t, "regresiva")) ||
                   (contains(&t, "cuanto") && contains(&t, "falta") && !contains(&t, "para"));
    if (!trigger) {
        return false;
    }
    bool mask[MAX_TOKENS] = {false};

    // ---- cancel
    bool cancel_word = contains(&t, "cancela") || contains(&t, "cancelar") || contains(&t, "quita") || contains(&t, "quitar") ||
                       contains(&t, "elimina") || contains(&t, "eliminar") || contains(&t, "borra") || contains(&t, "borrar") ||
                       contains(&t, "apaga") || contains(&t, "apagar");
    if (cancel_word) {
        int kind = (contains(&t, "temporizador") || contains(&t, "temporizadores")) ? ALARM_KIND_TIMER
                   : (contains(&t, "alarma") || contains(&t, "alarmas") || contains(&t, "despertador")) ? ALARM_KIND_ALARM
                   : (contains(&t, "recordatorio") || contains(&t, "recordatorios")) ? ALARM_KIND_REMINDER : 0;
        int removed = alarms_cancel_kind(kind);
        if (!removed) {
            ls_say(a, CLIP_NADA_CANCELAR);
            ls_finish(a, "No tienes nada programado para cancelar.", NULL);
        } else if (removed == 1 && kind == ALARM_KIND_TIMER) {
            ls_say(a, CLIP_CANCELE_TEMP);
            ls_finish(a, "Listo, cancelé el temporizador.", NULL);
        } else if (removed == 1 && kind == ALARM_KIND_ALARM) {
            ls_say(a, CLIP_CANCELE_ALARMA);
            ls_finish(a, "Listo, cancelé la alarma.", NULL);
        } else if (removed == 1 && kind == ALARM_KIND_REMINDER) {
            ls_say(a, CLIP_CANCELE_RECORD);
            ls_finish(a, "Listo, cancelé el recordatorio.", NULL);
        } else {
            ls_say(a, CLIP_CANCELE_TODO);
            ls_finish(a, "Listo, cancelé lo que tenías programado.", NULL);
        }
        return true;
    }

    int duration = parse_duration(&t, mask);
    clock_time_t clock = parse_clock(&t, mask);
    int weekday = -1;
    uint32_t repeat = parse_repeat(&t, mask, &weekday);

    // ---- list: "qué alarmas tengo", "cuánto falta"
    bool asks = contains(&t, "que") || contains(&t, "cuales") || contains(&t, "cuantos") || contains(&t, "cuantas") ||
                contains(&t, "mis") || contains(&t, "hay") || contains(&t, "falta");
    if (asks && !duration && !clock.found && !contains(&t, "pon") && !contains(&t, "ponme") && !contains(&t, "despiertame")) {
        int count = alarms_count();
        if (count == 0) {
            ls_say(a, CLIP_NADA_PROGRAMADO);
            ls_finish(a, "No tienes temporizadores ni alarmas.", NULL);
        } else if (count == 1) {
            ls_say(a, CLIP_UNA_COSA);
            ls_finish(a, "Tienes una cosa programada.", "rows");
            a->json = build_list_json();
        } else {
            ls_say(a, CLIP_TIENES);
            ls_say_number(a, count);
            ls_say(a, CLIP_COSAS);
            char reply[64];
            snprintf(reply, sizeof(reply), "Tienes %d cosas programadas.", count);
            ls_finish(a, reply, "rows");
            a->json = build_list_json();
        }
        if (!a->json) a->screen[0] = '\0';
        return true;
    }

    // Something to remember (a label) is for the PC, which can say it aloud
    if (!only_known_words(&t, mask)) {
        return false;
    }

    bool reminder = contains(&t, "recuerdame") || contains(&t, "avisame") || contains(&t, "recordatorio");
    time_t now = time(NULL);

    // ---- a length of time: a timer
    if (duration > 0 && !clock.found) {
        if (now < 1700000000) return false;
        uint8_t kind = reminder ? ALARM_KIND_REMINDER : ALARM_KIND_TIMER;
        alarms_add(alarms_new_id(), now + duration, reminder ? "Recordatorio" : "Temporizador", 0, kind);
        char reply[96] = "";
        ls_say(a, reminder ? CLIP_LISTO_RECORDARE : CLIP_LISTO_TEMPORIZADOR_DE);
        if (reminder) {
            ls_say(a, CLIP_EN);
            strlcpy(reply, "Listo, te recordaré en ", sizeof(reply));
        } else {
            strlcpy(reply, "Listo, temporizador de ", sizeof(reply));
        }
        say_duration(a, reply, sizeof(reply), duration);
        strlcat(reply, ".", sizeof(reply));
        ls_finish(a, reply, NULL);
        return true;
    }

    // ---- a clock time: an alarm
    if (clock.found && !duration) {
        if (now < 1700000000) return false;
        bool ambiguous = clock.ambiguous;
        int minute = clock.minute;
        struct tm local;
        localtime_r(&now, &local);
        time_t best = 0;
        int candidates[2] = {clock.hour, clock.hour + 12};
        for (int c = 0; c < (ambiguous ? 2 : 1); c++) {
            struct tm when = local;
            when.tm_hour = candidates[c];
            when.tm_min = minute;
            when.tm_sec = 0;
            when.tm_isdst = -1;
            if (clock.tomorrow) when.tm_mday += 1;
            time_t moment = mktime(&when);
            if (moment <= now + 30) {
                if (!clock.tomorrow && !ambiguous) moment += 86400;  // already passed today: the same time tomorrow
                else if (!clock.tomorrow) continue;
            }
            if (!best || moment < best) best = moment;
        }
        if (!best && ambiguous) {  // both candidates passed today: the earlier one tomorrow
            struct tm when = local;
            when.tm_hour = candidates[0];
            when.tm_min = minute;
            when.tm_sec = 0;
            when.tm_mday += 1;
            when.tm_isdst = -1;
            best = mktime(&when);
        }
        if (!best) return false;
        if (repeat == 604800 && weekday >= 0) {  // "todos los lunes": the next one of that weekday
            struct tm when;
            localtime_r(&best, &when);
            int current = (when.tm_wday + 6) % 7;
            best += (time_t)(((weekday - current + 7) % 7)) * 86400;
            if (best <= now + 30) best += 7 * 86400;
        }
        uint8_t kind = reminder ? ALARM_KIND_REMINDER : ALARM_KIND_ALARM;
        alarms_add(alarms_new_id(), best, reminder ? "Recordatorio" : "Alarma", repeat, kind);

        struct tm when;
        localtime_r(&best, &when);
        char reply[110];
        snprintf(reply, sizeof(reply), "%s ", reminder ? "Listo, te recordaré" : "Listo, te despierto");
        ls_say(a, reminder ? CLIP_LISTO_RECORDARE : CLIP_LISTO_DESPIERTO);
        if (repeat == 86400) {
            ls_say(a, CLIP_TODOS_LOS_DIAS);
            strlcat(reply, "todos los días ", sizeof(reply));
        } else if (repeat == 604800 && weekday >= 0) {
            ls_say(a, CLIP_EVERY_DAY0 + weekday);
            strlcat(reply, "todos los ", sizeof(reply));
            strlcat(reply, DAY_NAMES[weekday], sizeof(reply));
            strlcat(reply, " ", sizeof(reply));
        } else if (clock.tomorrow) {
            ls_say(a, CLIP_MANANA);
            strlcat(reply, "mañana ", sizeof(reply));
        }
        say_clock(a, reply, sizeof(reply), when.tm_hour, when.tm_min);
        strlcat(reply, ".", sizeof(reply));
        ls_finish(a, reply, NULL);
        return true;
    }

    // ---- not enough to go on
    ls_say(a, CLIP_PARA_CUANDO);
    ls_finish(a, "¿Para cuándo? Dime por ejemplo: pon un temporizador de 10 minutos, o despiértame a las 6 y media.", NULL);
    return true;
}

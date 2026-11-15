#include "local_skills.h"
#include "battery.h"
#include "local_skills_internal.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ------------------------------------------------------------------------------ text helpers
// Lowercase, no accents, no punctuation: "¿Qué hora es?" -> " que hora es "
static void normalize(const char *in, char *out, size_t size) {
    size_t o = 0;
    out[o++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 2 < size; p++) {
        char c = 0;
        if (*p == 0xC3 && p[1]) {  // Spanish letters in UTF-8
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
        } else if (isalnum(*p)) {
            c = (char)tolower(*p);
        } else {
            c = ' ';
        }
        if (c == ' ' && out[o - 1] == ' ') continue;
        out[o++] = c;
    }
    if (out[o - 1] != ' ') out[o++] = ' ';
    out[o] = '\0';
}

// The word (or the words) is in the text, between spaces
static bool has(const char *text, const char *word) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// A word that starts with this beginning ("su" matches sube, subir, suba...)
static bool has_start(const char *text, const char *start) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s", start);
    return strstr(text, padded) != NULL;
}

static bool any(const char *text, const char *const *words) {
    for (; *words; words++) {
        if (has(text, *words)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------- building answers
void ls_say(local_answer_t *a, int clip) {
    if (a->clip_count < LOCAL_MAX_CLIPS) a->clips[a->clip_count++] = clip;
}

// A whole number from 0 to 100 as words
void ls_say_number(local_answer_t *a, int n) {
    if (n < 0) n = 0;
    if (n > 100) n = 100;
    if (n < 30) {
        ls_say(a, CLIP_N0 + n);
    } else if (n == 100) {
        ls_say(a, CLIP_T100);
    } else {
        ls_say(a, CLIP_T30 + (n / 10 - 3));
        if (n % 10) {
            ls_say(a, CLIP_Y);
            ls_say(a, CLIP_N0 + n % 10);
        }
    }
}

void ls_finish(local_answer_t *a, const char *reply, const char *screen) {
    strlcpy(a->reply, reply, sizeof(a->reply));
    strlcpy(a->screen, screen ? screen : "", sizeof(a->screen));
}

static const char *const DAY_NAMES[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};
static const char *const MONTH_NAMES[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                                          "septiembre", "octubre", "noviembre", "diciembre"};

static bool now_local(struct tm *out) {
    time_t now = time(NULL);
    if (now < 1700000000) {  // the clock is not set yet
        return false;
    }
    localtime_r(&now, out);
    return true;
}

static void answer_time(local_answer_t *a) {
    struct tm t;
    if (!now_local(&t)) {
        ls_say(a, CLIP_NO_ENTENDI);
        ls_finish(a, "Todavía no sé la hora.", NULL);
        return;
    }
    int hour = t.tm_hour, minute = t.tm_min;
    int hour12 = hour % 12 == 0 ? 12 : hour % 12;
    char text[96];
    const char *period = hour < 6 ? "de la madrugada" : hour < 12 ? "de la mañana" : hour < 19 ? "de la tarde" : "de la noche";

    if (hour12 == 1) {
        ls_say(a, CLIP_ES_LA);
        ls_say(a, CLIP_UNA);
    } else {
        ls_say(a, CLIP_SON_LAS);
        ls_say_number(a, hour12);
    }
    if (minute == 0) {
        ls_say(a, CLIP_EN_PUNTO);
    } else if (minute == 15) {
        ls_say(a, CLIP_Y_CUARTO);
    } else if (minute == 30) {
        ls_say(a, CLIP_Y_MEDIA);
    } else {
        ls_say(a, CLIP_Y);
        ls_say_number(a, minute);
    }
    if (minute == 0 && hour == 12) {
        a->clip_count -= 1;  // "Son las 12 del mediodía": no "en punto"
        ls_say(a, CLIP_DEL_MEDIODIA);
        period = "del mediodía";
    } else if (minute == 0 && hour == 0) {
        a->clip_count -= 1;
        ls_say(a, CLIP_DE_LA_MEDIANOCHE);
        period = "de la medianoche";
    } else {
        ls_say(a, hour < 6 ? CLIP_DE_LA_MADRUGADA : hour < 12 ? CLIP_DE_LA_MANANA : hour < 19 ? CLIP_DE_LA_TARDE : CLIP_DE_LA_NOCHE);
    }

    const char *start = hour12 == 1 ? "Es la" : "Son las";
    if (minute == 0 && (hour == 12 || hour == 0)) {
        snprintf(text, sizeof(text), "%s 12 %s.", start, period);
    } else if (minute == 0) {
        snprintf(text, sizeof(text), "%s %d en punto %s.", start, hour12, period);
    } else if (minute == 15) {
        snprintf(text, sizeof(text), "%s %d y cuarto %s.", start, hour12, period);
    } else if (minute == 30) {
        snprintf(text, sizeof(text), "%s %d y media %s.", start, hour12, period);
    } else {
        snprintf(text, sizeof(text), "%s %d y %d %s.", start, hour12, minute, period);
    }
    ls_finish(a, text, NULL);
}

static void answer_date(local_answer_t *a) {
    struct tm t;
    if (!now_local(&t)) {
        ls_say(a, CLIP_NO_ENTENDI);
        ls_finish(a, "Todavía no sé la fecha.", NULL);
        return;
    }
    int weekday = (t.tm_wday + 6) % 7;  // Monday = 0
    ls_say(a, CLIP_HOY_ES);
    ls_say(a, CLIP_DAY0 + weekday);
    ls_say_number(a, t.tm_mday);
    ls_say(a, CLIP_DE);
    ls_say(a, CLIP_MONTH0 + t.tm_mon);
    char text[96];
    snprintf(text, sizeof(text), "Hoy es %s %d de %s.", DAY_NAMES[weekday], t.tm_mday, MONTH_NAMES[t.tm_mon]);
    ls_finish(a, text, NULL);
}

static void answer_battery(local_answer_t *a) {
    battery_status_t battery;
    if (!battery_read(&battery)) {
        return;
    }
    ls_say(a, CLIP_BATERIA_AL);
    ls_say_number(a, battery.percent);
    ls_say(a, CLIP_POR_CIENTO);
    char text[96];
    snprintf(text, sizeof(text), "La batería está al %d por ciento%s.", battery.percent, battery.charging ? " y se está cargando" : "");
    if (battery.charging) ls_say(a, CLIP_BATERIA_CARGANDO);
    ls_finish(a, text, NULL);
}

// A volume given as a number ("al 50", "al cincuenta"), or -1
static int volume_number(const char *text) {
    static const struct { const char *word; int value; } words[] = {
        {"diez", 10}, {"veinte", 20}, {"treinta", 30}, {"cuarenta", 40}, {"cincuenta", 50}, {"sesenta", 60},
        {"setenta", 70}, {"ochenta", 80}, {"noventa", 90}, {"cien", 100}};
    for (const char *p = text; *p; p++) {
        if (isdigit((unsigned char)*p) && (p == text || !isdigit((unsigned char)p[-1]))) {
            int v = atoi(p);
            return v > 100 ? 100 : v;
        }
    }
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (has(text, words[i].word)) return words[i].value;
    }
    return -1;
}

// ------------------------------------------------------------------------------------ the skills
// Politeness and filler around a request do not change what it asks: "oye Bug, ¿podrías decirme la hora, por favor?" is "la hora".
// Leading and trailing fillers are cut (only at the edges, so "qué puedes hacer" keeps its "puedes"), "por favor" anywhere.
static bool cut_prefix(char *text, const char *filler) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", filler);
    size_t n = strlen(padded);
    if (strncmp(text, padded, n) != 0) return false;
    memmove(text + 1, text + n, strlen(text + n) + 1);  // keep one leading space
    return true;
}

static bool cut_suffix(char *text, const char *filler) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", filler);
    size_t n = strlen(padded), len = strlen(text);
    if (len < n || strcmp(text + len - n, padded) != 0) return false;
    text[len - n + 1] = '\0';  // keep one trailing space
    return true;
}

static void strip_fillers(char *text) {
    static const char *const leading[] = {"oye", "oiga", "hola", "bug", "disculpa", "perdona", "una pregunta", "por favor", "porfa", "podrias", "podria",
                                          "puedes", "me puedes", "me podrias", "me podria", "quiero saber", "quisiera saber", "necesito saber",
                                          "me gustaria saber", "sera que", "ey", "hey", "bueno", "entonces", "mira", "decirme", "decime", "contarme", "decir", NULL};
    static const char *const trailing[] = {"por favor", "porfa", "gracias", "ahora", "ahorita", "ya", "bug", "hola", "mil gracias", "por fa", NULL};
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; leading[i]; i++) changed |= cut_prefix(text, leading[i]);
        for (int i = 0; trailing[i]; i++) changed |= cut_suffix(text, trailing[i]);
        for (char *at; (at = strstr(text, " por favor ")) != NULL;) {  // "pon la radio por favor ahora"
            memmove(at + 1, at + 11, strlen(at + 11) + 1);
            changed = true;
        }
    }
}

bool local_skills_try(const char *original, local_answer_t *a) {
    char text[200];
    normalize(original, text, sizeof(text));
    strip_fillers(text);
    memset(a, 0, sizeof(*a));

    static const char *const music_words[] = {"musica", "radio", "emisora", "cancion", NULL};
    static const char *const stop_words[] = {"para", "parar", "deten", "detener", "apaga", "apagar", "quita", "quitar", "pausa", "pausar", "cierra", NULL};
    static const char *const timer_words[] = {"temporizador", "alarma", "despiertame", "recuerdame", "avisame", "minutos", "minuto", "horas", "segundos", NULL};
    static const char *const home_words[] = {"inicio", "volver", "vuelve", "atras", "salir", "principal", "menu", "home", "regresa", "regresar", NULL};

    if (local_help_try(text, a)) {
        return true;
    }
    if (local_claude_try(text, a)) {
        return true;
    }
    // The owner's own skills from the card come before the built-in ones, so a phrase of theirs wins
    if (local_sdskills_try(text, a)) {
        return true;
    }
    // The smart home: only claims a sentence when it names a device from the card's list
    if (local_home_try(text, a)) {
        return true;
    }
    if (local_voiceid_try(original, text, a)) {
        return true;
    }
    // The shopping list and the notes (the board keeps them itself)
    if (local_lists_try(original, a)) {
        return true;
    }
    // Timers, alarms and reminders (the board keeps them itself)
    if (local_timers_try(text, a)) {
        return true;
    }
    if (local_holidays_try(text, a)) {
        return true;
    }
    if (local_weather_try(text, a)) {
        return true;
    }
    if (local_mail_try(text, a)) {
        return true;
    }
    if (local_style_try(text, a)) {
        return true;
    }
    if (local_calendar_try(text, a)) {
        return true;
    }
    if (local_radio_try(original, text, a)) {
        return true;
    }
    if (local_math_try(text, a) || local_prices_try(text, a)) {
        return true;
    }
    bool about_timers = any(text, timer_words);  // what the board could not parse alone goes to the PC

    if (has(text, "cronometro")) {
        bool reset = has(text, "reinicia") || has(text, "reiniciar") || has(text, "resetea") || has(text, "cero") || has(text, "borra");
        bool stop = has(text, "para") || has(text, "parar") || has(text, "deten") || has(text, "detener") || has(text, "pausa") || has(text, "pausar");
        if (reset) {
            ls_say(a, CLIP_CRONO_CERO);
            ls_finish(a, "Listo, cronómetro en cero.", "stopwatch?cmd=reset");
        } else if (stop) {
            ls_say(a, CLIP_CRONO_DETENIDO);
            ls_finish(a, "Cronómetro detenido.", "stopwatch?cmd=pause");
        } else {
            ls_say(a, CLIP_CRONO_MARCHA);
            ls_finish(a, "Cronómetro en marcha.", "stopwatch?cmd=start");
        }
        return true;
    }
    if (has(text, "triqui") || (has(text, "tres") && (has(text, "raya") || has(text, "linea")))) {
        ls_say(a, CLIP_JUGUEMOS_TRIQUI);
        ls_finish(a, "Juguemos al triqui. Tú eres la equis, toca una casilla.", "triqui?cmd=new");
        return true;
    }
    if (has(text, "volumen") || has(text, "mas alto") || has(text, "mas fuerte") || has(text, "mas bajo") ||
        has(text, "mas suave") || has(text, "mas despacio")) {
        int number = has(text, "volumen") ? volume_number(text) : -1;
        if (number >= 5 && has(text, "volumen")) {
            char screen[48];
            snprintf(screen, sizeof(screen), "volume?v=%d", number);
            ls_say(a, CLIP_VOLUMEN_AL);
            ls_say_number(a, number);
            ls_say(a, CLIP_POR_CIENTO);
            char reply[96];
            snprintf(reply, sizeof(reply), "Listo, volumen al %d por ciento.", number);
            ls_finish(a, reply, screen);
            return true;
        }
        bool down = has_start(text, "baj") || has_start(text, "disminu") || has(text, "mas bajo") || has(text, "mas suave") ||
                    has(text, "mas despacio");
        bool up = has_start(text, "su") || has_start(text, "aument") || has(text, "mas alto") || has(text, "mas fuerte");
        if (down) {
            ls_say(a, CLIP_LISTO_BAJE);
            ls_finish(a, "Listo, bajé el volumen.", "volume?d=-15");
            return true;
        }
        if (up) {
            ls_say(a, CLIP_LISTO_SUBI);
            ls_finish(a, "Listo, subí el volumen.", "volume?d=15");
            return true;
        }
    }
    if (any(text, music_words) && any(text, stop_words) && !about_timers) {
        ls_say(a, CLIP_LISTO_APAGUE_MUSICA);
        ls_finish(a, "Listo, apagué la música.", "music_stop");
        return true;
    }
    if (has(text, "silencio") && !any(text, music_words)) {
        ls_say(a, CLIP_LISTO_APAGUE_MUSICA);
        ls_finish(a, "Listo, apagué la música.", "music_stop");
        return true;
    }
    // Asking the time or the date in the many ways people do. "horas" and "minutos" alone do not make it a timer here:
    // only the words that really set one (temporizador, alarma, avisame...) keep a sentence away from the clock.
    static const char *const timer_strict[] = {"temporizador", "alarma", "despiertame", "recuerdame", "avisame", "cuenta", NULL};
    bool sets_timer = any(text, timer_strict);
    if (!sets_timer && (has(text, "que hora") || has(text, "que horas") || has(text, "la hora") || has(text, "hora es") || has(text, "hora son") ||
                        has(text, "horas son") || has(text, "hora tienes") || has(text, "tienes hora") || has(text, "hora actual") ||
                        has(text, "hora exacta") || has(text, "hora sera") || has(text, "hora local") || has(text, "dime la hora") ||
                        has(text, "dame la hora") || (has(text, "hora") && strlen(text) < 14))) {
        answer_time(a);
        return true;
    }
    if (!sets_timer && (has(text, "que dia es") || has(text, "la fecha") || has(text, "que fecha") || has(text, "que dia es hoy") ||
                        has(text, "que dia estamos") || has(text, "a que dia estamos") || has(text, "a cuanto estamos") ||
                        has(text, "en que fecha") || has(text, "fecha de hoy") || has(text, "dia de hoy") || has(text, "hoy es que dia") ||
                        has(text, "que dia de la semana") || has(text, "fecha actual") || has(text, "que dia cae hoy") ||
                        (has(text, "fecha") && strlen(text) < 14))) {
        answer_date(a);
        return true;
    }
    if (has(text, "bateria") && !has(text, "calendario")) {
        answer_battery(a);
        return a->clip_count > 0;
    }
    if (any(text, home_words) && !any(text, music_words) && strlen(text) < 40) {
        ls_say(a, CLIP_LISTO);
        ls_finish(a, "Listo.", "home");
        return true;
    }
    // Jokes, the news and Wikipedia: shown, and said with the free online voice
    if (local_more_try(original, text, a)) {
        return true;
    }
    return local_online_try(original, text, a);
}

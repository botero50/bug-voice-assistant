#include "local_skills_internal.h"
#include "voice_clips.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Colombian public holidays, computed by the board (no data, no internet): fixed dates, dates moved to the next
// Monday (Ley Emiliani) and the ones that follow Easter.

typedef struct {
    long day;  // days since 1970-01-01
    int id;    // index of the holiday's name (HOLIDAY0 + id)
} holiday_t;

static long days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, int *year, int *month, int *day) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (int)(y + (*month <= 2));
}

static long easter(int year) {
    int a = year % 19, b = year / 100, c = year % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
    return days_from_civil(year, month, day);
}

static long to_monday(long day) {
    int weekday = (int)((day + 3) % 7 + 7) % 7;  // 1970-01-01 was a Thursday; Monday = 0
    return day + (7 - weekday) % 7;
}

static int holidays_of(int year, holiday_t *out) {
    long pascua = easter(year);
    int n = 0;
#define ADD(d, i) out[n].day = (d); out[n].id = (i); n++
    ADD(days_from_civil(year, 1, 1), 0);
    ADD(to_monday(days_from_civil(year, 1, 6)), 1);
    ADD(to_monday(days_from_civil(year, 3, 19)), 2);
    ADD(pascua - 3, 3);
    ADD(pascua - 2, 4);
    ADD(days_from_civil(year, 5, 1), 5);
    ADD(to_monday(pascua + 39), 6);
    ADD(to_monday(pascua + 60), 7);
    ADD(to_monday(pascua + 68), 8);
    ADD(to_monday(days_from_civil(year, 6, 29)), 9);
    ADD(days_from_civil(year, 7, 20), 10);
    ADD(days_from_civil(year, 8, 7), 11);
    ADD(to_monday(days_from_civil(year, 8, 15)), 12);
    ADD(to_monday(days_from_civil(year, 10, 12)), 13);
    ADD(to_monday(days_from_civil(year, 11, 1)), 14);
    ADD(to_monday(days_from_civil(year, 11, 11)), 15);
    ADD(days_from_civil(year, 12, 8), 16);
    ADD(days_from_civil(year, 12, 25), 17);
#undef ADD
    return n;
}

static const char *const NAMES[] = {"Año Nuevo", "Día de los Reyes Magos", "Día de San José", "Jueves Santo", "Viernes Santo",
                                    "Día del Trabajo", "Ascensión del Señor", "Corpus Christi", "Sagrado Corazón",
                                    "San Pedro y San Pablo", "Día de la Independencia", "Batalla de Boyacá",
                                    "Asunción de la Virgen", "Día de la Raza", "Todos los Santos",
                                    "Independencia de Cartagena", "Inmaculada Concepción", "Navidad"};
static const char *const DAYS[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};
static const char *const MONTHS[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                                     "septiembre", "octubre", "noviembre", "diciembre"};

bool local_holidays_try(const char *text, local_answer_t *a) {
    if (!strstr(text, " festivo ") && !strstr(text, " festivos ") && !strstr(text, " feriado ")) {
        return false;
    }
    time_t now = time(NULL);
    if (now < 1700000000) {
        return false;
    }
    struct tm local;
    localtime_r(&now, &local);
    long today = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);

    holiday_t next = {0, -1};
    for (int year = local.tm_year + 1900; year <= local.tm_year + 1901 && next.id < 0; year++) {
        holiday_t list[18];
        int n = holidays_of(year, list);
        for (int i = 0; i < n; i++) {
            if (list[i].day >= today && (next.id < 0 || list[i].day < next.day)) next = list[i];
        }
    }
    int year, month, day;
    civil_from_days(next.day, &year, &month, &day);
    int weekday = (int)((next.day + 3) % 7 + 7) % 7;
    int left = (int)(next.day - today);

    ls_say(a, CLIP_PROXIMO_FESTIVO);
    ls_say(a, CLIP_HOLIDAY0 + next.id);
    char reply[200];
    int used = snprintf(reply, sizeof(reply), "El próximo festivo es %s", NAMES[next.id]);
    if (left == 0) {
        ls_say(a, CLIP_HOY);
        snprintf(reply + used, sizeof(reply) - used, ", hoy.");
    } else if (left == 1) {
        ls_say(a, CLIP_MANANA_PUNTO);
        snprintf(reply + used, sizeof(reply) - used, ", mañana.");
    } else {
        ls_say(a, CLIP_EL);
        ls_say(a, CLIP_DAY0 + weekday);
        ls_say_number(a, day);
        ls_say(a, CLIP_DE);
        ls_say(a, CLIP_MONTH0 + month - 1);
        used += snprintf(reply + used, sizeof(reply) - used, ", el %s %d de %s", DAYS[weekday], day, MONTHS[month - 1]);
        if (left <= 100) {
            ls_say(a, CLIP_DENTRO_DE);
            ls_say_number(a, left);
            ls_say(a, CLIP_DIAS);
            snprintf(reply + used, sizeof(reply) - used, ", dentro de %d días.", left);
        }
    }
    ls_finish(a, reply, NULL);
    return true;
}

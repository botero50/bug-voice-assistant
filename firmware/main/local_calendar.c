#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "nvs.h"
#include "settings_store.h"
#include "voice_clips.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The agenda, read by the board itself from calendar links in ICS format (Google Calendar "secret address in iCal
// format", Outlook "published calendar"). The links live in the board's memory; they can be typed in Settings or
// copied once from the PC's calendar_sources.txt. Handled: single events, all-day events, UTC and local times,
// daily / weekly / monthly / yearly repeats with INTERVAL, COUNT, UNTIL and EXDATE.

static const char *TAG = "CALENDAR";

#define MAX_EVENTS 300
#define CACHE_SECONDS 300
#define COLOMBIA_OFFSET (5 * 3600)  // local = UTC - 5 hours, no daylight saving

typedef struct {
    long day;       // local day, days since 1970-01-01
    int minutes;    // minutes after midnight
    bool all_day;
    char title[56];
    char place[32];
    char freq;      // 0 none, 'D', 'W', 'M', 'Y'
    int interval;
    int count;      // 0 = no limit
    long until;     // 0 = no limit
    uint8_t byday;  // bit 0 = Monday
    int exdate_count;
    long exdate[6];
} event_t;

static event_t *events = NULL;
static int event_count = 0;
static time_t fetched_at = 0;
static char urls[400];
static bool urls_loaded = false;
static bool sync_tried = false;

// ---------------------------------------------------------------------------------------------- dates
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

static int weekday_of(long day) {
    return (int)((day + 3) % 7 + 7) % 7;  // Monday = 0
}

// "20260115T153000Z", "20260115T153000" or "20260115": the local day and minute
static bool parse_when(const char *value, bool *all_day, long *day, int *minutes) {
    int y, m, d, hh = 0, mm = 0, ss = 0;
    if (sscanf(value, "%4d%2d%2d", &y, &m, &d) != 3) return false;
    const char *t = strchr(value, 'T');
    if (!t) {
        *all_day = true;
        *day = days_from_civil(y, m, d);
        *minutes = 0;
        return true;
    }
    if (sscanf(t + 1, "%2d%2d%2d", &hh, &mm, &ss) < 2) return false;
    *all_day = false;
    long seconds = days_from_civil(y, m, d) * 86400L + hh * 3600L + mm * 60L + ss;
    if (value[strlen(value) - 1] == 'Z') seconds -= COLOMBIA_OFFSET;  // UTC -> local
    *day = (seconds >= 0 ? seconds : seconds - 86399) / 86400;
    long rest = seconds - *day * 86400L;
    *minutes = (int)(rest / 60);
    return true;
}

// ---------------------------------------------------------------------------------------------- parsing
static void unescape_into(char *out, size_t size, const char *in) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < size; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            char c = *p;
            out[o++] = (c == 'n' || c == 'N' || c == '\\') ? ' ' : c;
        } else if (*p == '"' || *p == '\r' || *p == '\n' || (unsigned char)*p < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = *p;
        }
    }
    // do not leave half a UTF-8 letter at the end
    while (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0x80) o--;
    if (o > 0 && (unsigned char)out[o - 1] >= 0xC0) o--;
    out[o] = '\0';
}

static void parse_rule(event_t *e, const char *rule) {
    char copy[160];
    strlcpy(copy, rule, sizeof(copy));
    char *save = NULL;
    e->interval = 1;
    for (char *part = strtok_r(copy, ";", &save); part; part = strtok_r(NULL, ";", &save)) {
        if (strncmp(part, "FREQ=", 5) == 0) {
            const char *f = part + 5;
            e->freq = strcmp(f, "DAILY") == 0 ? 'D' : strcmp(f, "WEEKLY") == 0 ? 'W' : strcmp(f, "MONTHLY") == 0 ? 'M' : strcmp(f, "YEARLY") == 0 ? 'Y' : 0;
        } else if (strncmp(part, "INTERVAL=", 9) == 0) {
            e->interval = atoi(part + 9) > 0 ? atoi(part + 9) : 1;
        } else if (strncmp(part, "COUNT=", 6) == 0) {
            e->count = atoi(part + 6);
        } else if (strncmp(part, "UNTIL=", 6) == 0) {
            bool all;
            int minutes;
            long day;
            if (parse_when(part + 6, &all, &day, &minutes)) e->until = day;
        } else if (strncmp(part, "BYDAY=", 6) == 0) {
            static const char *const NAMES[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
            for (int i = 0; i < 7; i++) {
                if (strstr(part + 6, NAMES[i])) e->byday |= (uint8_t)(1 << i);
            }
        }
    }
}

// The value of "NAME;params:value" when the line starts with NAME; NULL otherwise
static const char *value_of(const char *line, const char *name) {
    size_t n = strlen(name);
    if (strncmp(line, name, n) != 0 || (line[n] != ':' && line[n] != ';')) return NULL;
    const char *colon = strchr(line, ':');
    return colon ? colon + 1 : NULL;
}

static void add_events_from(char *text) {
    // Unfold in place: a line that starts with a space or tab continues the one before
    char *w = text;
    for (char *r = text; *r; r++) {
        if (*r == '\r') continue;
        if (*r == '\n' && (r[1] == ' ' || r[1] == '\t')) {
            r++;
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';

    event_t current;
    bool inside = false, cancelled = false, has_start = false, is_exception = false;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strcmp(line, "BEGIN:VEVENT") == 0) {
            memset(&current, 0, sizeof(current));
            current.interval = 1;
            strlcpy(current.title, "Sin título", sizeof(current.title));
            inside = true;
            cancelled = has_start = is_exception = false;
        } else if (strcmp(line, "END:VEVENT") == 0) {
            if (inside && has_start && !cancelled && !is_exception && event_count < MAX_EVENTS) events[event_count++] = current;
            inside = false;
        } else if (inside) {
            const char *v;
            if ((v = value_of(line, "DTSTART"))) {
                has_start = parse_when(v, &current.all_day, &current.day, &current.minutes);
            } else if ((v = value_of(line, "SUMMARY"))) {
                unescape_into(current.title, sizeof(current.title), v);
            } else if ((v = value_of(line, "LOCATION"))) {
                unescape_into(current.place, sizeof(current.place), v);
            } else if ((v = value_of(line, "STATUS"))) {
                cancelled = strncmp(v, "CANCELLED", 9) == 0;
            } else if (value_of(line, "RECURRENCE-ID")) {
                is_exception = true;
            } else if ((v = value_of(line, "RRULE"))) {
                parse_rule(&current, v);
            } else if ((v = value_of(line, "EXDATE"))) {
                char copy[120];
                strlcpy(copy, v, sizeof(copy));
                char *s2 = NULL;
                for (char *p = strtok_r(copy, ",", &s2); p && current.exdate_count < 6; p = strtok_r(NULL, ",", &s2)) {
                    bool all;
                    int minutes;
                    long day;
                    if (parse_when(p, &all, &day, &minutes)) current.exdate[current.exdate_count++] = day;
                }
            }
        }
    }
}

static bool matches(const event_t *e, long day) {
    switch (e->freq) {
    case 'D':
        return (day - e->day) % e->interval == 0;
    case 'W': {
        long weeks = ((day - weekday_of(day)) - (e->day - weekday_of(e->day))) / 7;
        uint8_t mask = e->byday ? e->byday : (uint8_t)(1 << weekday_of(e->day));
        return weeks % e->interval == 0 && (mask & (1 << weekday_of(day)));
    }
    case 'M': {
        int y1, m1, d1, y2, m2, d2;
        civil_from_days(e->day, &y1, &m1, &d1);
        civil_from_days(day, &y2, &m2, &d2);
        return d1 == d2 && ((y2 - y1) * 12 + m2 - m1) % e->interval == 0;
    }
    case 'Y': {
        int y1, m1, d1, y2, m2, d2;
        civil_from_days(e->day, &y1, &m1, &d1);
        civil_from_days(day, &y2, &m2, &d2);
        return m1 == m2 && d1 == d2 && (y2 - y1) % e->interval == 0;
    }
    }
    return false;
}

static bool occurs(const event_t *e, long day) {
    if (day < e->day) return false;
    for (int i = 0; i < e->exdate_count; i++) {
        if (e->exdate[i] == day) return false;
    }
    if (!e->freq) return day == e->day;
    if (e->until && day > e->until) return false;
    if (!e->count) return matches(e, day);
    int seen = 0;  // COUNT needs counting the occurrences from the start
    for (long d = e->day, steps = 0; d <= day && steps < 20000; d++, steps++) {
        if (matches(e, d)) {
            bool excluded = false;
            for (int i = 0; i < e->exdate_count; i++) excluded |= e->exdate[i] == d;
            if (!excluded && ++seen > e->count) return false;
            if (d == day) return !excluded;
        }
    }
    return false;
}

// -------------------------------------------------------------------------------------------- the links
static void load_urls(void) {
    if (urls_loaded) return;
    urls_loaded = true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(urls);
        if (nvs_get_str(handle, "ics", urls, &size) != ESP_OK) urls[0] = '\0';
        nvs_close(handle);
    }
}

bool local_calendar_configured(void) {
    load_urls();
    return urls[0] != '\0';
}

void local_calendar_set_urls(const char *list) {
    urls_loaded = true;
    strlcpy(urls, list ? list : "", sizeof(urls));
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "ics", urls);
        nvs_commit(handle);
        nvs_close(handle);
    }
    fetched_at = 0;
    ESP_LOGI(TAG, "Calendar links saved (%d characters)", (int)strlen(urls));
}

// Copies the links once from the PC (its calendar_sources.txt), so they need not be typed on the board
static void sync_from_pc(void) {
    if (sync_tried) return;
    sync_tried = true;
    bug_settings_t settings;
    settings_copy(&settings);
    char url[160];
    snprintf(url, sizeof(url), "%s/config/calendar", settings.server_url);
    char *body = ls_http_get(url, false);
    if (!body) return;
    size_t n = strlen(body);
    while (n > 0 && (body[n - 1] == '\n' || body[n - 1] == '\r' || body[n - 1] == ' ')) body[--n] = '\0';
    if (n > 8 && n < sizeof(urls)) local_calendar_set_urls(body);
    heap_caps_free(body);
}

static bool refresh(void) {
    time_t now = time(NULL);
    if (fetched_at && now - fetched_at < CACHE_SECONDS) return true;
    if (!events) events = heap_caps_malloc(MAX_EVENTS * sizeof(event_t), MALLOC_CAP_SPIRAM);
    if (!events) return false;
    int before_count = event_count;
    event_count = 0;
    char copy[sizeof(urls)];
    strlcpy(copy, urls, sizeof(copy));
    char *save = NULL;
    bool any = false;
    for (char *link = strtok_r(copy, ";", &save); link; link = strtok_r(NULL, ";", &save)) {
        char fixed[400];
        if (strncmp(link, "webcal://", 9) == 0) snprintf(fixed, sizeof(fixed), "https://%s", link + 9);
        else strlcpy(fixed, link, sizeof(fixed));
        char *body = ls_http_get(fixed, strncmp(fixed, "https", 5) == 0);
        if (!body) continue;
        add_events_from(body);
        heap_caps_free(body);
        any = true;
    }
    if (!any) {
        event_count = before_count;  // no internet: keep what was read before
        return fetched_at != 0;
    }
    fetched_at = now;
    ESP_LOGI(TAG, "%d events read", event_count);
    return true;
}

// ---------------------------------------------------------------------------------------------- answers
static const char *const DAYS[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};
static const char *const MONTHS[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};

static int collect(long day, const event_t **out, int max) {
    int n = 0;
    for (int i = 0; i < event_count; i++) {
        if (occurs(&events[i], day) && n < max) out[n++] = &events[i];
    }
    for (int i = 1; i < n; i++) {  // all-day first, then by time
        const event_t *x = out[i];
        int j = i - 1;
        while (j >= 0 && ((out[j]->all_day ? -1 : out[j]->minutes) > (x->all_day ? -1 : x->minutes))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = x;
    }
    return n;
}

static void clock_text(int minutes, char *out, size_t size) {
    int h = minutes / 60, m = minutes % 60, h12 = h % 12 == 0 ? 12 : h % 12;
    snprintf(out, size, "%d:%02d %s", h12, m, h < 12 ? "a. m." : "p. m.");
}

static void say_time(local_answer_t *a, int minutes) {
    int h = minutes / 60, m = minutes % 60, h12 = h % 12 == 0 ? 12 : h % 12;
    ls_say(a, h12 == 1 ? CLIP_A_LA : CLIP_A_LAS);
    ls_say_number(a, h12);
    if (m == 15) ls_say(a, CLIP_Y_CUARTO);
    else if (m == 30) ls_say(a, CLIP_Y_MEDIA);
    else if (m) {
        ls_say(a, CLIP_Y);
        ls_say_number(a, m);
    }
    ls_say(a, h < 6 ? CLIP_DE_LA_MADRUGADA : h < 12 ? CLIP_DE_LA_MANANA : h < 19 ? CLIP_DE_LA_TARDE : CLIP_DE_LA_NOCHE);
}

static char *build_json(long today) {
    char *json = heap_caps_malloc(2600, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    size_t used = (size_t)snprintf(json, 2600, "{\"type\":\"calendar\",\"sample\":false,\"days\":[");
    for (int offset = 0; offset < 2; offset++) {
        long day = today + offset;
        int y, m, d;
        civil_from_days(day, &y, &m, &d);
        const event_t *list[4];
        int n = collect(day, list, 4);
        used += (size_t)snprintf(json + used, 2600 - used, "%s{\"label\":\"%s · %s %d de %s\",\"events\":[", offset ? "," : "",
                                 offset ? "Mañana" : "Hoy", DAYS[weekday_of(day)], d, MONTHS[m - 1]);
        for (int i = 0; i < n && used < 2200; i++) {
            char when[16];
            if (list[i]->all_day) strlcpy(when, "Todo el día", sizeof(when));
            else clock_text(list[i]->minutes, when, sizeof(when));
            used += (size_t)snprintf(json + used, 2600 - used, "%s{\"time\":\"%s\",\"title\":\"%s\",\"place\":\"%s\"}", i ? "," : "", when,
                                     list[i]->title, list[i]->place);
        }
        used += (size_t)snprintf(json + used, 2600 - used, "]}");
    }
    snprintf(json + used, 2600 - used, "]}");
    return json;
}

static bool has(const char *text, const char *word) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// A sentence about today's agenda for the morning briefing; false when no calendar is set up or it cannot be read
bool local_calendar_brief(char *out, size_t size) {
    if (!local_calendar_configured() || !refresh()) return false;
    long today = (long)((time(NULL) - COLOMBIA_OFFSET) / 86400);
    const event_t *list[4];
    int n = collect(today, list, 4);
    if (n == 0) {
        snprintf(out, size, "Hoy no tienes eventos.");
    } else if (list[0]->all_day) {
        snprintf(out, size, "Hoy tienes %d evento%s. %s, todo el día.", n, n == 1 ? "" : "s", list[0]->title);
    } else {
        char when[16];
        clock_text(list[0]->minutes, when, sizeof(when));
        snprintf(out, size, "Hoy tienes %d evento%s. El primero es %s, a las %s.", n, n == 1 ? "" : "s", list[0]->title, when);
    }
    return true;
}

bool local_calendar_try(const char *text, local_answer_t *a) {
    bool asked = has(text, "calendario") || has(text, "agenda") || has(text, "reunion") || has(text, "reuniones") || has(text, "cita") ||
                 has(text, "citas") || has(text, "evento") || has(text, "eventos") || strstr(text, " que tengo hoy ") ||
                 strstr(text, " que tengo manana ") || strstr(text, " que tengo para hoy ") || strstr(text, " que tengo para manana ");
    if (!asked) return false;
    if (!local_calendar_configured()) sync_from_pc();
    if (!local_calendar_configured()) return false;  // nothing to read: the PC shows its sample agenda

    if (!refresh()) {
        ls_say(a, CLIP_SIN_CONEXION);
        ls_finish(a, "No tengo conexión en este momento.", NULL);
        return true;
    }
    time_t now = time(NULL);
    long today = (long)((now - COLOMBIA_OFFSET) / 86400);
    const event_t *list[4];
    int n_today = collect(today, list, 4);
    int n_tomorrow = 0;
    {
        const event_t *tomorrow[4];
        n_tomorrow = collect(today + 1, tomorrow, 4);
    }

    char reply[96];
    if (n_today == 0) {
        ls_say(a, CLIP_HOY_NO_EVENTOS);
        snprintf(reply, sizeof(reply), "Hoy no tienes eventos.");
    } else {
        ls_say(a, CLIP_HOY_TIENES);
        if (n_today == 1) {
            ls_say(a, CLIP_UN_EVENTO);
        } else {
            ls_say_number(a, n_today);
            ls_say(a, CLIP_EVENTOS_N);
        }
        ls_say(a, CLIP_PRIMERO_ES);
        char when[16];
        if (list[0]->all_day) {
            ls_say(a, CLIP_TODO_EL_DIA);
            strlcpy(when, "todo el día", sizeof(when));
        } else {
            say_time(a, list[0]->minutes);
            clock_text(list[0]->minutes, when, sizeof(when));
        }
        snprintf(reply, sizeof(reply), "Hoy tienes %d evento%s. El primero es %s.", n_today, n_today == 1 ? "" : "s", when);
    }
    if (n_tomorrow > 0 && a->clip_count < 24) {
        ls_say(a, CLIP_MANANA_TIENES);
        ls_say_number(a, n_tomorrow);
        ls_say(a, CLIP_MAS_PUNTO);
    }
    ls_finish(a, reply, "calendar");
    a->json = build_json(today);
    if (!a->json) a->screen[0] = '\0';
    return true;
}

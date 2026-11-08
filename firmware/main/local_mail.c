#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "local_skills_internal.h"
#include "nvs.h"
#include "settings_store.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Email read by the board itself over IMAP (TLS, port 993): the newest messages with who sent them, the subject and
// the time, plus how many are unread. Messages are read with PEEK, so nothing is marked as read. The mailbox
// (host, user and an app password) is kept in the board's memory; it is copied once from the PC's mail_accounts.json.
// Reading a message aloud needs a text-to-speech, so "lee el primero" is still the PC's job.

static const char *TAG = "MAIL";

#define SHOWN 4
#define CACHE_SECONDS 90
#define BUFFER_BYTES 20000
#define COLOMBIA_OFFSET (5 * 3600)

typedef struct {
    char from[40];
    char subject[72];
    char when[16];
    bool unread;
    long stamp;  // for sorting, seconds since 1970 (UTC)
    int seq;     // the message's number in the mailbox
} mail_t;

static mail_t mails[SHOWN];
static int mail_count = 0, unread_count = 0;
static time_t fetched_at = 0;
static char account[3][96];  // host, user, password
static bool account_loaded = false, sync_tried = false;

// ------------------------------------------------------------------------------------------ the account
static void load_account(void) {
    if (account_loaded) return;
    account_loaded = true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        char packed[300];
        size_t size = sizeof(packed);
        if (nvs_get_str(handle, "mail", packed, &size) == ESP_OK) {
            char *save = NULL;
            int i = 0;
            for (char *part = strtok_r(packed, "\n", &save); part && i < 3; part = strtok_r(NULL, "\n", &save)) strlcpy(account[i++], part, sizeof(account[0]));
        }
        nvs_close(handle);
    }
}

static bool configured(void) {
    load_account();
    return account[0][0] && account[1][0] && account[2][0];
}

static void save_account(const char *host, const char *user, const char *password) {
    strlcpy(account[0], host, sizeof(account[0]));
    strlcpy(account[1], user, sizeof(account[1]));
    strlcpy(account[2], password, sizeof(account[2]));
    account_loaded = true;
    char packed[300];
    snprintf(packed, sizeof(packed), "%s\n%s\n%s", host, user, password);
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "mail", packed);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Mailbox saved for %s", user);
}

// Set from the installer page (config_serial.c); empty text removes the mailbox
bool local_mail_set_account(const char *host, const char *user, const char *password) {
    save_account(host, user, password);
    fetched_at = 0;
    return true;
}

void local_mail_user(char *out, size_t size) {
    load_account();
    strlcpy(out, account[1], size);
}

// Copies the mailbox once from the PC, so the password need not be typed on the board
static void sync_from_pc(void) {
    if (sync_tried) return;
    sync_tried = true;
    bug_settings_t settings;
    settings_copy(&settings);
    char url[160];
    snprintf(url, sizeof(url), "%s/config/mail", settings.server_url);
    char *body = ls_http_get(url, false);
    if (!body) return;
    char *save = NULL;
    char *parts[3] = {NULL, NULL, NULL};
    int i = 0;
    for (char *line = strtok_r(body, "\r\n", &save); line && i < 3; line = strtok_r(NULL, "\r\n", &save)) parts[i++] = line;
    if (i == 3 && parts[0][0] && parts[1][0] && parts[2][0]) save_account(parts[0], parts[1], parts[2]);
    heap_caps_free(body);
}

// ------------------------------------------------------------------------------------------ the session
static int tls_send(esp_tls_t *tls, const char *text) {
    size_t left = strlen(text);
    while (left > 0) {
        int sent = esp_tls_conn_write(tls, text, left);
        if (sent <= 0) return -1;
        text += sent;
        left -= (size_t)sent;
    }
    return 0;
}

// Reads until the line that ends the command (`tag` followed by OK, NO or BAD). Returns true on OK.
static bool read_response(esp_tls_t *tls, const char *tag, char *buffer, size_t cap) {
    size_t used = 0;
    buffer[0] = '\0';
    char marker_ok[12], marker_no[12], marker_bad[12];
    snprintf(marker_ok, sizeof(marker_ok), "\r\n%s OK", tag);
    snprintf(marker_no, sizeof(marker_no), "\r\n%s NO", tag);
    snprintf(marker_bad, sizeof(marker_bad), "\r\n%s BAD", tag);
    for (int idle = 0; idle < 60 && used + 1 < cap;) {
        int got = esp_tls_conn_read(tls, buffer + used, cap - used - 1);
        if (got == ESP_TLS_ERR_SSL_WANT_READ || got == ESP_TLS_ERR_SSL_WANT_WRITE) {
            idle++;
            continue;
        }
        if (got <= 0) return false;
        used += (size_t)got;
        buffer[used] = '\0';
        // the tag can also start the very first line of the buffer
        if (strstr(buffer, marker_ok) || strncmp(buffer, marker_ok + 2, strlen(marker_ok) - 2) == 0) return true;
        if (strstr(buffer, marker_no) || strstr(buffer, marker_bad)) return false;
    }
    return false;
}

// ------------------------------------------------------------------------------------------- decoding
static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int base64_value(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static void append_utf8(char *out, size_t size, size_t *o, unsigned code) {
    if (code < 0x80 && *o + 1 < size) {
        out[(*o)++] = (char)code;
    } else if (code < 0x800 && *o + 2 < size) {
        out[(*o)++] = (char)(0xC0 | (code >> 6));
        out[(*o)++] = (char)(0x80 | (code & 0x3F));
    }
}

// "=?UTF-8?B?...?=" and "=?ISO-8859-1?Q?...?=" words become plain UTF-8 text; everything else is copied
static void decode_words(const char *in, char *out, size_t size) {
    size_t o = 0;
    const char *p = in;
    bool after_word = false;
    while (*p && o + 4 < size) {
        if (p[0] == '=' && p[1] == '?') {
            const char *charset_end = strchr(p + 2, '?');
            const char *enc = charset_end ? charset_end + 1 : NULL;
            const char *text = enc && enc[0] && enc[1] == '?' ? enc + 2 : NULL;
            const char *end = text ? strstr(text, "?=") : NULL;
            if (end) {
                bool latin = strncasecmp(p + 2, "iso-8859", 8) == 0 || strncasecmp(p + 2, "windows-125", 11) == 0;
                bool base64 = toupper((unsigned char)enc[0]) == 'B';
                unsigned bits = 0;
                int have = 0;
                for (const char *c = text; c < end && o + 4 < size; c++) {
                    if (base64) {
                        int v = base64_value((unsigned char)*c);
                        if (v < 0) continue;
                        bits = (bits << 6) | (unsigned)v;
                        have += 6;
                        if (have >= 8) {
                            have -= 8;
                            unsigned byte = (bits >> have) & 0xFF;
                            if (latin) append_utf8(out, size, &o, byte);
                            else out[o++] = (char)byte;
                        }
                    } else {
                        unsigned byte;
                        if (*c == '_') byte = ' ';
                        else if (*c == '=' && c + 2 < end + 1 && hex_value(c[1]) >= 0 && hex_value(c[2]) >= 0) {
                            byte = (unsigned)(hex_value(c[1]) * 16 + hex_value(c[2]));
                            c += 2;
                        } else byte = (unsigned char)*c;
                        if (latin) append_utf8(out, size, &o, byte);
                        else out[o++] = (char)byte;
                    }
                }
                p = end + 2;
                after_word = true;
                continue;
            }
        }
        if (after_word && (*p == ' ' || *p == '\t')) {  // the blank between two encoded words is not text
            const char *q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (q[0] == '=' && q[1] == '?') {
                p = q;
                continue;
            }
        }
        after_word = false;
        out[o++] = *p++;
    }
    out[o] = '\0';
    // trim a half UTF-8 letter at the end and any control characters
    for (size_t i = 0; i < o; i++) {
        if ((unsigned char)out[i] < 0x20) out[i] = ' ';
    }
    while (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0x80) o--;
    if (o > 0 && (unsigned char)out[o - 1] >= 0xC0) o--;
    out[o] = '\0';
    for (size_t i = 0; i < o; i++) {
        if (out[i] == '"' || out[i] == '\\') out[i] = ' ';  // would break the JSON the screen reads
    }
}

static long days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

// "Sun, 8 Nov 2026 09:15:00 -0500" -> seconds since 1970 (UTC)
static long parse_date(const char *value) {
    static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char *p = strchr(value, ',');
    p = p ? p + 1 : value;
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char month_name[8] = "";
    if (sscanf(p, " %d %3s %d %d:%d:%d", &day, month_name, &year, &hh, &mm, &ss) < 5) return 0;
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(month_name, MONTHS[i], 3) == 0) month = i + 1;
    }
    if (!month) return 0;
    if (year < 100) year += 2000;
    long seconds = days_from_civil(year, month, day) * 86400L + hh * 3600L + mm * 60L + ss;
    const char *zone = strrchr(p, ' ');
    if (zone && (zone[1] == '+' || zone[1] == '-') && isdigit((unsigned char)zone[2])) {
        int offset = atoi(zone + 1);  // like -0500
        int sign = offset < 0 ? -1 : 1;
        offset = offset < 0 ? -offset : offset;
        seconds -= sign * ((offset / 100) * 3600L + (offset % 100) * 60L);
    }
    return seconds;
}

static void time_label(long stamp, char *out, size_t size) {
    if (stamp <= 0) {
        out[0] = '\0';
        return;
    }
    static const char *const MONTHS[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
    long local = stamp - COLOMBIA_OFFSET;
    long day = local / 86400, now_day = (long)(time(NULL) - COLOMBIA_OFFSET) / 86400;
    int minutes = (int)((local - day * 86400L) / 60);
    if (day == now_day) {
        snprintf(out, size, "%d:%02d %s", minutes / 60 % 12 == 0 ? 12 : minutes / 60 % 12, minutes % 60, minutes / 60 < 12 ? "am" : "pm");
    } else if (day == now_day - 1) {
        strlcpy(out, "ayer", size);
    } else {
        time_t t = (time_t)(local);
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(out, size, "%d %s", tm.tm_mday, MONTHS[tm.tm_mon]);
    }
}

// The value of a header ("Subject") in a block of header lines, unfolded, or an empty string
static void header_value(const char *block, size_t length, const char *name, char *out, size_t size) {
    out[0] = '\0';
    size_t n = strlen(name);
    const char *end = block + length;
    for (const char *line = block; line < end;) {
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        if (!eol) eol = end;
        if ((size_t)(eol - line) > n + 1 && strncasecmp(line, name, n) == 0 && line[n] == ':') {
            size_t o = 0;
            const char *p = line + n + 1;
            while (1) {
                while (p < eol && (*p == ' ' || *p == '\t')) p++;
                const char *stop = eol;
                if (stop > p && stop[-1] == '\r') stop--;
                size_t take = (size_t)(stop - p);
                if (o + take + 2 >= size) take = size - o - 2 > 0 ? size - o - 2 : 0;
                if (o) out[o++] = ' ';
                memcpy(out + o, p, take);
                o += take;
                if (eol >= end || (eol[1] != ' ' && eol[1] != '\t')) break;  // a folded line continues the value
                p = eol + 1;
                eol = memchr(p, '\n', (size_t)(end - p));
                if (!eol) eol = end;
            }
            out[o] = '\0';
            return;
        }
        line = eol + 1;
    }
}

static int compare_mails(const void *a, const void *b) {
    long x = ((const mail_t *)a)->stamp, y = ((const mail_t *)b)->stamp;
    return x < y ? 1 : x > y ? -1 : 0;
}

// Parses the "* n FETCH (FLAGS (...) BODY[HEADER.FIELDS (...)] {len}\r\n<headers>)" blocks of the answer
static void parse_fetch(const char *text) {
    mail_count = 0;
    const char *p = text;
    while ((p = strstr(p, " FETCH (")) && mail_count < SHOWN) {
        const char *line_end = strstr(p, "\r\n");
        const char *brace = strchr(p, '{');
        if (!line_end || !brace || brace > line_end) break;
        size_t length = (size_t)atoi(brace + 1);
        const char *data = line_end + 2;
        char flags[80] = "";
        const char *f = strstr(p, "FLAGS (");
        if (f && f < line_end) {
            size_t k = 0;
            for (f += 7; *f && *f != ')' && k + 1 < sizeof(flags); f++) flags[k++] = *f;
            flags[k] = '\0';
        }
        if (!flags[0]) {  // some servers send the flags after the headers
            const char *after = data + length;
            const char *g = strstr(after, "FLAGS (");
            const char *next = strstr(after, " FETCH (");
            if (g && (!next || g < next)) {
                size_t k = 0;
                for (g += 7; *g && *g != ')' && k + 1 < sizeof(flags); g++) flags[k++] = *g;
                flags[k] = '\0';
            }
        }
        mail_t *m = &mails[mail_count];
        {
            const char *digits = p;
            while (digits > text && isdigit((unsigned char)digits[-1])) digits--;
            m->seq = atoi(digits);
        }
        char raw[160], date[48];
        header_value(data, length, "From", raw, sizeof(raw));
        // "Ana Gómez <ana@x.com>" -> the name; with no name, the address
        char *lt = strchr(raw, '<');
        char name_part[160];
        if (lt && lt > raw) {
            size_t n = (size_t)(lt - raw);
            memcpy(name_part, raw, n);
            name_part[n] = '\0';
        } else {
            strlcpy(name_part, lt ? lt + 1 : raw, sizeof(name_part));
            char *gt = strchr(name_part, '>');
            if (gt) *gt = '\0';
        }
        decode_words(name_part, m->from, sizeof(m->from));
        for (int n = (int)strlen(m->from) - 1; n >= 0 && m->from[n] == ' '; n--) m->from[n] = 48 - 48;
        header_value(data, length, "Subject", raw, sizeof(raw));
        decode_words(raw, m->subject, sizeof(m->subject));
        if (!m->subject[0]) strlcpy(m->subject, "(sin asunto)", sizeof(m->subject));
        header_value(data, length, "Date", date, sizeof(date));
        m->stamp = parse_date(date);
        time_label(m->stamp, m->when, sizeof(m->when));
        m->unread = strstr(flags, "\\Seen") == NULL;
        mail_count++;
        p = data + length;
    }
    qsort(mails, (size_t)mail_count, sizeof(mails[0]), compare_mails);
    unread_count = 0;
    for (int i = 0; i < mail_count; i++) unread_count += mails[i].unread;
}

// ------------------------------------------------------------------------------- reading one message
// Turns the text of a message (as the mail server sends it) into plain text: MIME parts, quoted-printable or base64,
// ISO-8859-1 or UTF-8, HTML with its tags removed.

static void squeeze(char *s) {
    char *w = s;
    bool space = true;
    bool line_start = true, quoted = false;
    for (char *r = s; *r; r++) {
        if (line_start && *r == '>') quoted = true;  // lines of a quoted older message are not read
        if (*r == '\n') {
            line_start = true;
            quoted = false;
            if (!space) {
                *w++ = ' ';
                space = true;
            }
            continue;
        }
        if (*r != ' ' && *r != '\t' && *r != '\r') line_start = false;
        if (quoted) continue;
        if (*r == ' ' || *r == '\t' || *r == '\r') {
            if (!space) {
                *w++ = ' ';
                space = true;
            }
        } else {
            *w++ = *r;
            space = false;
        }
    }
    *w = '\0';
}

static void quoted_printable(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '=' && r[1] == '\r' && r[2] == '\n') {
            r += 2;
        } else if (*r == '=' && r[1] == '\n') {
            r += 1;
        } else if (*r == '=' && hex_value(r[1]) >= 0 && hex_value(r[2]) >= 0) {
            *w++ = (char)(hex_value(r[1]) * 16 + hex_value(r[2]));
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static void base64_text(char *s) {
    char *w = s;
    unsigned bits = 0;
    int have = 0;
    for (char *r = s; *r; r++) {
        if (*r == '=') break;
        int v = base64_value((unsigned char)*r);
        if (v < 0) continue;
        bits = (bits << 6) | (unsigned)v;
        have += 6;
        if (have >= 8) {
            have -= 8;
            *w++ = (char)((bits >> have) & 0xFF);
        }
    }
    *w = '\0';
}

static void latin1_to_utf8(char *s, size_t size) {
    char *copy = heap_caps_malloc(strlen(s) + 1, MALLOC_CAP_SPIRAM);
    if (!copy) return;
    strcpy(copy, s);
    size_t o = 0;
    for (const unsigned char *r = (const unsigned char *)copy; *r; r++) append_utf8(s, size, &o, *r);
    s[o] = '\0';
    heap_caps_free(copy);
}

static void strip_html(char *s) {
    char *w = s;
    bool in_tag = false;
    for (char *r = s; *r; r++) {
        if (*r == '<') {
            in_tag = true;
            if (strncasecmp(r, "<br", 3) == 0 || strncasecmp(r, "</p", 3) == 0 || strncasecmp(r, "</div", 5) == 0) *w++ = '\n';
            if (strncasecmp(r, "<style", 6) == 0 || strncasecmp(r, "<script", 7) == 0) {
                const char *end = strcasestr(r, strncasecmp(r, "<style", 6) == 0 ? "</style>" : "</script>");
                if (end) r = (char *)end;
            }
        } else if (*r == '>') {
            in_tag = false;
        } else if (!in_tag) {
            if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' '; r += 5; }
            else if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 4; }
            else if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 3; }
            else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 3; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = ' '; r += 5; }
            else if (*r == '&') {
                static const struct { const char *name, *utf8; } ENTITIES[] = {
                    {"&aacute;", "á"}, {"&eacute;", "é"}, {"&iacute;", "í"}, {"&oacute;", "ó"}, {"&uacute;", "ú"}, {"&ntilde;", "ñ"}, {"&uuml;", "ü"},
                    {"&Aacute;", "Á"}, {"&Eacute;", "É"}, {"&Iacute;", "Í"}, {"&Oacute;", "Ó"}, {"&Uacute;", "Ú"}, {"&Ntilde;", "Ñ"},
                    {"&iquest;", "¿"}, {"&iexcl;", "¡"}, {"&apos;", "'"}, {"&#39;", "'"}, {"&euro;", "euros"}};
                bool known = false;
                for (size_t k = 0; k < sizeof(ENTITIES) / sizeof(ENTITIES[0]) && !known; k++) {
                    size_t n = strlen(ENTITIES[k].name);
                    if (strncmp(r, ENTITIES[k].name, n) == 0) {
                        for (const char *u = ENTITIES[k].utf8; *u; u++) *w++ = *u;
                        r += n - 1;
                        known = true;
                    }
                }
                if (!known) *w++ = *r;
            } else *w++ = *r;
        }
    }
    *w = '\0';
}

// A message part: its header block and its body. Writes the plain text into `out` and says whether it came from HTML.
static bool decode_part(const char *headers, size_t hlen, const char *body, size_t blen, int depth, char *out, size_t size, bool *html) {
    char type[200], encoding[40];
    header_value(headers, hlen, "Content-Type", type, sizeof(type));
    header_value(headers, hlen, "Content-Transfer-Encoding", encoding, sizeof(encoding));
    if (strcasestr(type, "multipart/") && depth < 3) {
        const char *b = strcasestr(type, "boundary=");
        if (!b) return false;
        b += 9;
        char boundary[100] = "--";
        size_t n = 2;
        bool quoted = *b == '"';
        if (quoted) b++;
        while (*b && *b != '"' && (quoted || (*b != ';' && *b != ' ')) && n + 1 < sizeof(boundary)) boundary[n++] = *b++;
        boundary[n] = '\0';
        char *plain = heap_caps_malloc(size, MALLOC_CAP_SPIRAM), *rich = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        char *tmp = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        bool have_plain = false, have_rich = false;
        if (plain && rich && tmp) {
            const char *p = body, *end = body + blen;
            while ((p = strstr(p, boundary)) && p < end) {
                p += strlen(boundary);
                if (p[0] == '-' && p[1] == '-') break;  // the closing boundary
                const char *line = strstr(p, "\n");
                if (!line) break;
                const char *part = line + 1;
                const char *next = strstr(part, boundary);
                size_t part_len = next ? (size_t)(next - part) : (size_t)(end - part);
                const char *split = strstr(part, "\r\n\r\n");
                size_t gap = 4;
                if (!split || (size_t)(split - part) > part_len) {
                    split = strstr(part, "\n\n");
                    gap = 2;
                }
                if (split && (size_t)(split - part) + gap <= part_len) {
                    bool part_html = false;
                    if (decode_part(part, (size_t)(split - part), split + gap, part_len - (size_t)(split - part) - gap, depth + 1, tmp, size, &part_html)) {
                        if (!part_html && !have_plain) {
                            strlcpy(plain, tmp, size);
                            have_plain = true;
                        } else if (part_html && !have_rich) {
                            strlcpy(rich, tmp, size);
                            have_rich = true;
                        }
                    }
                }
                p = next ? next : end;
            }
        }
        heap_caps_free(tmp);
        bool ok = false;
        if (have_plain) { strlcpy(out, plain, size); *html = false; ok = true; }
        else if (have_rich) { strlcpy(out, rich, size); *html = true; ok = true; }
        heap_caps_free(plain);
        heap_caps_free(rich);
        return ok;
    }
    if (type[0] && !strcasestr(type, "text/")) return false;  // an attachment or an image
    char *text = heap_caps_malloc(blen + 1, MALLOC_CAP_SPIRAM);
    if (!text) return false;
    memcpy(text, body, blen);
    text[blen] = '\0';
    if (strcasestr(encoding, "base64")) base64_text(text);
    else if (strcasestr(encoding, "quoted-printable")) quoted_printable(text);
    bool is_html = strcasestr(type, "text/html") != NULL;
    if (is_html) strip_html(text);
    squeeze(text);
    strlcpy(out, text, size);
    if (strcasestr(type, "iso-8859") || strcasestr(type, "windows-125")) latin1_to_utf8(out, size);
    heap_caps_free(text);
    *html = is_html;
    return out[0] != '\0';
}

// Connects, logs in and opens the inbox read-only. Returns the connection (and the number of messages in `total`) or NULL.
static esp_tls_t *open_session(char *buffer, int *total) {
    esp_tls_cfg_t cfg = {.crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 12000};
    esp_tls_t *tls = esp_tls_init();
    if (!tls) return NULL;
    if (esp_tls_conn_new_sync(account[0], (int)strlen(account[0]), 993, &cfg, tls) <= 0) {
        ESP_LOGW(TAG, "Could not connect to %s", account[0]);
        esp_tls_conn_destroy(tls);
        return NULL;
    }
    if (esp_tls_conn_read(tls, buffer, 200) <= 0) {  // the greeting
        esp_tls_conn_destroy(tls);
        return NULL;
    }
    // The user and password go inside quotes; a quote or backslash inside them is escaped
    char user[100], password[100], command[220];
    size_t u = 0, w = 0;
    for (const char *c = account[1]; *c && u + 2 < sizeof(user); c++) {
        if (*c == '"' || *c == '\\') user[u++] = '\\';
        user[u++] = *c;
    }
    user[u] = '\0';
    for (const char *c = account[2]; *c && w + 2 < sizeof(password); c++) {
        if (*c == '"' || *c == '\\') password[w++] = '\\';
        password[w++] = *c;
    }
    password[w] = '\0';
    snprintf(command, sizeof(command), "A1 LOGIN \"%s\" \"%s\"\r\n", user, password);
    if (tls_send(tls, command) || !read_response(tls, "A1", buffer, BUFFER_BYTES)) {
        ESP_LOGW(TAG, "The mail server did not accept the login");
        esp_tls_conn_destroy(tls);
        return NULL;
    }
    if (tls_send(tls, "A2 EXAMINE INBOX\r\n") || !read_response(tls, "A2", buffer, BUFFER_BYTES)) {
        esp_tls_conn_destroy(tls);
        return NULL;
    }
    const char *exists = strstr(buffer, " EXISTS");
    *total = 0;
    if (exists) {
        const char *s = exists;
        while (s > buffer && isdigit((unsigned char)s[-1])) s--;
        *total = atoi(s);
    }
    return tls;
}

static bool fetch_mail(void) {
    time_t now = time(NULL);
    if (fetched_at && now - fetched_at < CACHE_SECONDS) return true;
    char *buffer = heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_SPIRAM);
    if (!buffer) return false;
    int total = 0;
    esp_tls_t *tls = open_session(buffer, &total);
    bool ok = false;
    if (tls) {
        if (total > 0) {
            char command[220];
            int first = total > SHOWN ? total - SHOWN + 1 : 1;
            snprintf(command, sizeof(command), "A3 FETCH %d:%d (FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])\r\n", first, total);
            if (!tls_send(tls, command) && read_response(tls, "A3", buffer, BUFFER_BYTES)) {
                parse_fetch(buffer);
                ok = true;
            }
        } else {
            mail_count = unread_count = 0;
            ok = true;
        }
        tls_send(tls, "A4 LOGOUT\r\n");
        esp_tls_conn_destroy(tls);
    }
    heap_caps_free(buffer);
    if (ok) {
        fetched_at = now;
        ESP_LOGI(TAG, "%d messages, %d unread", mail_count, unread_count);
    }
    return ok;
}

// The text of one message (about the first 6000 bytes of it), ready to be read aloud
static bool fetch_body(int seq, char *out, size_t size) {
    char *buffer = heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_SPIRAM);
    if (!buffer) return false;
    int total = 0;
    esp_tls_t *tls = open_session(buffer, &total);
    bool ok = false;
    if (tls) {
        char command[200];
        snprintf(command, sizeof(command), "A5 FETCH %d (BODY.PEEK[HEADER.FIELDS (CONTENT-TYPE CONTENT-TRANSFER-ENCODING)] BODY.PEEK[TEXT]<0.6000>)\r\n", seq);
        if (!tls_send(tls, command) && read_response(tls, "A5", buffer, BUFFER_BYTES)) {
            const char *brace = strchr(buffer, '{');
            if (brace) {
                size_t header_length = (size_t)atoi(brace + 1);
                const char *headers = strstr(brace, "\r\n");
                headers = headers ? headers + 2 : NULL;
                const char *second = headers ? strchr(headers + header_length, '{') : NULL;
                if (headers && second) {
                    size_t body_length = (size_t)atoi(second + 1);
                    const char *body = strstr(second, "\r\n");
                    if (body) {
                        body += 2;
                        bool html = false;
                        ok = decode_part(headers, header_length, body, body_length, 0, out, size, &html);
                    }
                }
            }
        }
        tls_send(tls, "A6 LOGOUT\r\n");
        esp_tls_conn_destroy(tls);
    }
    heap_caps_free(buffer);
    return ok;
}

// ------------------------------------------------------------------------------------------- the skill
static bool has(const char *text, const char *word) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

static char *build_json(void) {
    char *json = heap_caps_malloc(1800, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    size_t used = (size_t)snprintf(json, 1800, "{\"type\":\"email_list\",\"sample\":false,\"unread\":%d,\"items\":[", unread_count);
    for (int i = 0; i < mail_count; i++) {
        used += (size_t)snprintf(json + used, 1800 - used,
                                 "%s{\"from\":\"%s\",\"subject\":\"%s\",\"time\":\"%s\",\"unread\":%s,\"body\":\"Dime: lee el correo, para escucharlo.\"}",
                                 i ? "," : "", mails[i].from, mails[i].subject, mails[i].when, mails[i].unread ? "true" : "false");
    }
    snprintf(json + used, 1800 - used, "]}");
    return json;
}

bool local_mail_try(const char *text, local_answer_t *a) {
    bool about_mail = has(text, "correo") || has(text, "correos") || has(text, "email") || has(text, "emails") || has(text, "mensajes") ||
                      has(text, "bandeja") || has(text, "gmail");
    if (!about_mail) return false;
    // Answering, writing and deleting are not done on the board: those go to the PC
    if (has(text, "responde") || has(text, "contesta") || has(text, "escribe") || has(text, "envia") || has(text, "manda") ||
        has(text, "borra") || has(text, "elimina")) {
        return false;
    }
    bool read_aloud = has(text, "lee") || has(text, "leer") || has(text, "leeme") || has(text, "leame") || has(text, "lea");
    if (!configured()) sync_from_pc();
    if (!configured()) return false;  // the PC shows its sample mailbox

    if (read_aloud) {
        int index = 0;
        if (has(text, "segundo") || has(text, "segunda") || has(text, "dos") || has(text, "2")) index = 1;
        else if (has(text, "tercer") || has(text, "tercero") || has(text, "tercera") || has(text, "tres") || has(text, "3")) index = 2;
        else if (has(text, "cuarto") || has(text, "cuarta") || has(text, "cuatro") || has(text, "4")) index = 3;
        if (!fetch_mail() && !fetched_at) {
            ls_say(a, CLIP_SIN_CONEXION);
            ls_finish(a, "No tengo conexión en este momento.", NULL);
            return true;
        }
        if (index >= mail_count) {
            ls_finish(a, "No tienes tantos correos.", NULL);
            ls_say(a, CLIP_LISTO);
            return true;
        }
        static char body[360];
        body[0] = '\0';
        bool got = fetch_body(mails[index].seq, body, sizeof(body));
        char spoken[420];
        int used = snprintf(spoken, sizeof(spoken), "Correo de %s. Asunto: %s. ", mails[index].from, mails[index].subject);
        if (got) {
            // the first sentences that fit
            size_t room = sizeof(spoken) - (size_t)used - 1;
            size_t n = strlen(body);
            if (n > room) {
                n = room;
                size_t cut = 0;
                for (size_t i = 0; i < n; i++) {
                    if ((body[i] == '.' || body[i] == '!' || body[i] == '?') && body[i + 1] == ' ') cut = i + 1;
                }
                if (cut > room / 3) n = cut;
                while (n > 0 && ((unsigned char)body[n] & 0xC0) == 0x80) n--;
            }
            memcpy(spoken + used, body, n);
            spoken[used + (int)n] = '\0';
        } else {
            snprintf(spoken + used, sizeof(spoken) - (size_t)used, "No pude leer el texto de este correo.");
        }
        ls_speak(a, spoken);
        ls_finish(a, "Leyendo el correo.", NULL);
        return true;
    }

    if (!fetch_mail() && !fetched_at) {
        ls_say(a, CLIP_SIN_CONEXION);
        ls_finish(a, "No tengo conexión en este momento.", NULL);
        return true;
    }
    char reply[96];
    if (unread_count == 0) {
        ls_say(a, CLIP_NO_CORREOS_NUEVOS);
        snprintf(reply, sizeof(reply), "No tienes correos sin leer.");
    } else if (unread_count == 1) {
        ls_say(a, CLIP_UN_CORREO_NUEVO);
        snprintf(reply, sizeof(reply), "Tienes un correo sin leer.");
    } else {
        ls_say(a, CLIP_TIENES);
        ls_say_number(a, unread_count);
        ls_say(a, CLIP_CORREOS_NUEVOS);
        snprintf(reply, sizeof(reply), "Tienes %d correos sin leer.", unread_count);
    }
    ls_finish(a, reply, "email");
    a->json = build_json();
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// Skills written by the owner as plain text files on the microSD card: no compiling, no flashing.
//
// Put one or more .txt files in the folder  /sd/skills  of the card. Each line is one skill:
//
//     phrase | another phrase => what Bug answers
//     phrase => first answer || second answer       (one of them, at random)
//
// - Lines that start with # and empty lines are ignored.
// - The phrases are matched against what you said ignoring capitals, accents and punctuation: "hola mundo" fires on
//   "Bug, ¡hola, mundo!" and on "dile hola mundo a todos".
// - The answer may use {hora} and {fecha}, which the board fills in from its clock.
// - The answer is shown on the screen and said with the free online voice (it needs internet; without it the text is shown only).
// - These skills are checked before the built-in ones, so a phrase of yours wins over a built-in one.
#include "local_context.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef SDSKILLS_HOST
#ifdef _WIN32
#define localtime_r(timer, result) (localtime_s((result), (timer)) ? NULL : (result))  // Windows host test only
#endif
#else
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
static const char *TAG = "SDSKILLS";
#endif

#ifndef SDSKILLS_FOLDER
#define SDSKILLS_FOLDER "/sd/skills"
#endif
#define MAX_FILE_BYTES (16 * 1024)

static const char *const DAYS[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
static const char *const MONTHS[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                                     "septiembre", "octubre", "noviembre", "diciembre"};

// Copies `src` to `out`, filling {hora} and {fecha}
static void fill_placeholders(const char *src, size_t length, char *out, size_t size) {
    size_t o = 0;
    time_t now = time(NULL);
    struct tm t = {0};
    bool clock_ok = now > 1700000000;
    if (clock_ok) localtime_r(&now, &t);
    for (size_t i = 0; i < length && o + 1 < size;) {
        char piece[64] = "";
        size_t skip = 0;
        if (src[i] == '{') {
            if (strncmp(src + i, "{hora}", 6) == 0) {
                skip = 6;
                if (clock_ok) {
                    int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
                    snprintf(piece, sizeof(piece), "%d:%02d %s", h12, t.tm_min, t.tm_hour < 12 ? "de la mañana" : t.tm_hour < 19 ? "de la tarde" : "de la noche");
                } else {
                    snprintf(piece, sizeof(piece), "(sin hora todavía)");
                }
            } else if (strncmp(src + i, "{fecha}", 7) == 0) {
                skip = 7;
                if (clock_ok) {
                    snprintf(piece, sizeof(piece), "%s %d de %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
                } else {
                    snprintf(piece, sizeof(piece), "(sin fecha todavía)");
                }
            }
        }
        if (skip) {
            for (const char *p = piece; *p && o + 1 < size; p++) out[o++] = *p;
            i += skip;
        } else {
            out[o++] = src[i++];
        }
    }
    out[o] = '\0';
}

static void trim(const char **start, const char **end) {
    while (*start < *end && isspace((unsigned char)**start)) (*start)++;
    while (*end > *start && isspace((unsigned char)(*end)[-1])) (*end)--;
}

// Does any of the phrases (the part of the line before "=>", separated by '|') appear in the sentence? `sentence` is already normalized.
static bool phrases_match(const char *from, const char *to, const char *sentence) {
    const char *p = from;
    while (p <= to) {
        const char *bar = p;
        while (bar < to && *bar != '|') bar++;
        const char *a = p, *b = bar;
        trim(&a, &b);
        if (b - a >= 3) {
            char raw[96], norm[112];
            size_t n = (size_t)(b - a) < sizeof(raw) - 1 ? (size_t)(b - a) : sizeof(raw) - 1;
            memcpy(raw, a, n);
            raw[n] = '\0';
            local_context_normalize(raw, norm, sizeof(norm));
            if (strlen(norm) > 2 && strstr(sentence, norm)) return true;
        }
        p = bar + 1;
    }
    return false;
}

// Looks for the sentence in the text of a skills file. `pick` chooses among answers separated by "||".
bool sdskills_match(const char *file_text, const char *sentence, char *reply, size_t size, unsigned pick) {
    const char *line = file_text;
    while (*line) {
        const char *end = strchr(line, '\n');
        const char *next = end ? end + 1 : line + strlen(line);
        if (!end) end = line + strlen(line);
        const char *a = line, *b = end;
        if (b > a && b[-1] == '\r') b--;
        trim(&a, &b);
        if (a < b && *a != '#') {
            const char *arrow = NULL;
            for (const char *q = a; q + 1 < b; q++) {
                if (q[0] == '=' && q[1] == '>') { arrow = q; break; }
            }
            if (arrow && phrases_match(a, arrow, sentence)) {
                const char *answer = arrow + 2, *answer_end = b;
                trim(&answer, &answer_end);
                // the variants, split by "||"
                int count = 1;
                for (const char *q = answer; q + 1 < answer_end; q++) {
                    if (q[0] == '|' && q[1] == '|') count++;
                }
                int want = (int)(pick % (unsigned)count);
                const char *start = answer;
                for (const char *q = answer; q + 1 < answer_end && want > 0; q++) {
                    if (q[0] == '|' && q[1] == '|') { start = q + 2; want--; q++; }
                }
                const char *stop = start;
                while (stop < answer_end && !(stop + 1 < answer_end && stop[0] == '|' && stop[1] == '|')) stop++;
                trim(&start, &stop);
                if (stop > start) {
                    fill_placeholders(start, (size_t)(stop - start), reply, size);
                    return true;
                }
            }
        }
        line = next;
    }
    return false;
}

#ifndef SDSKILLS_HOST
#include <dirent.h>

bool local_sdskills_try(const char *text, local_answer_t *a) {
    DIR *dir = opendir(SDSKILLS_FOLDER);
    if (!dir) return false;  // no card or no folder: nothing to do
    char *buffer = heap_caps_malloc(MAX_FILE_BYTES + 1, MALLOC_CAP_SPIRAM);  // 16 KB: not from the scarce internal RAM
    if (!buffer) {
        closedir(dir);
        return false;
    }
    bool found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 5 || strcasecmp(entry->d_name + name_length - 4, ".txt") != 0 || entry->d_name[0] == '.') continue;
        char path[300];
        snprintf(path, sizeof(path), SDSKILLS_FOLDER "/%s", entry->d_name);
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        size_t n = fread(buffer, 1, MAX_FILE_BYTES, file);
        fclose(file);
        buffer[n] = '\0';
        char reply[400];
        if (sdskills_match(buffer, text, reply, sizeof(reply), (unsigned)time(NULL))) {
            ESP_LOGI(TAG, "Skill from %s: \"%s\"", entry->d_name, reply);
            ls_finish(a, reply, NULL);
            ls_speak(a, reply);
            found = true;
        }
    }
    free(buffer);
    closedir(dir);
    return found;
}
#endif

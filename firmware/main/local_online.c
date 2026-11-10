#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_random.h"
#include <string.h>

// Skills the board does by itself that end in words it has no recording of: jokes (stored on the board), Wikipedia
// and the news (fetched by the board from free services). They are shown on a screen and said with the free online
// voice (tts_online.c) when there is internet.

static const char *TAG = "ONLINE";

void ls_speak(local_answer_t *a, const char *text) {
    strlcpy(a->speak, text, sizeof(a->speak));
    a->speak_lang[0] = '\0';
}

void ls_speak_lang(local_answer_t *a, const char *text, const char *lang) {
    strlcpy(a->speak, text, sizeof(a->speak));
    strlcpy(a->speak_lang, lang, sizeof(a->speak_lang));
}

static bool has(const char *text, const char *word) {
    char padded[48];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// A string made safe to sit inside JSON text
static void json_text(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const char *p = in; *p && o + 3 < size; p++) {
        if (*p == '"' || *p == '\\') out[o++] = ' ';
        else if (*p == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if ((unsigned char)*p < 0x20) out[o++] = ' ';
        else out[o++] = *p;
    }
    out[o] = '\0';
}

// The first sentences of a text, up to `limit` characters
static void first_sentences(const char *text, size_t limit, char *out, size_t size) {
    size_t n = strlen(text);
    if (limit >= size) limit = size - 1;
    if (n <= limit) {
        strlcpy(out, text, size);
        return;
    }
    size_t cut = 0;
    for (size_t i = 0; i < limit; i++) {
        if ((text[i] == '.' || text[i] == '!' || text[i] == '?') && (text[i + 1] == ' ' || text[i + 1] == '\0')) cut = i + 1;
    }
    if (cut < limit / 3) {
        cut = limit;
        while (cut > 0 && text[cut] != ' ') cut--;
    }
    while (cut > 0 && cut < n && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;
    memcpy(out, text, cut);
    out[cut] = '\0';
}

char *ls_article_json(const char *title, const char *text, const char *footer) {
    char *json = heap_caps_malloc(900, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    char t[100], b[420], f[40];
    json_text(title, t, sizeof(t));
    json_text(text, b, sizeof(b));
    json_text(footer, f, sizeof(f));
    snprintf(json, 900, "{\"type\":\"article\",\"title\":\"%s\",\"text\":\"%s\",\"footer\":\"%s\"}", t, b, f);
    return json;
}

// ------------------------------------------------------------------------------------------- jokes
static const char *const JOKES[][2] = {
    {"¿Por qué los pájaros vuelan hacia el sur en invierno?", "Porque caminando tardarían mucho."},
    {"¿Qué le dice un cero a otro cero?", "Nada, no valemos nada."},
    {"¿Cuál es el colmo de un electricista?", "Que su mujer se llame Luz y sus hijos le sigan la corriente."},
    {"¿Qué hace una abeja en el gimnasio?", "¡Zum-ba!"},
    {"¿Por qué estaba triste el libro de matemáticas?", "Porque tenía muchos problemas."},
    {"¿Cuál es el colmo de un jardinero?", "Que su novia se llame Rosa y lo deje plantado."},
    {"¿Qué le dice una iguana a su hermana gemela?", "Somos iguanitas."},
    {"¿Qué le dice un techo a otro techo?", "Techo de menos."},
    {"¿Por qué los esqueletos no pelean entre ellos?", "Porque no tienen agallas."},
    {"¿Cuál es el café más peligroso del mundo?", "El ex-preso."},
    {"¿Qué le dice un gusano a otro gusano?", "Voy a dar una vuelta a la manzana."},
    {"¿Cómo se llama el campeón de buceo japonés?", "Tokofondo. ¿Y el subcampeón? Kasitoko."},
};
static int last_joke = -1;

// -------------------------------------------------------------------------------------------- news
static void unescape_entities(char *s) {
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '&') {
            if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; continue; }
            if (strncmp(r, "&quot;", 6) == 0) { *w++ = ' '; r += 6; continue; }
            if (strncmp(r, "&#39;", 5) == 0 || strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += r[2] == '3' ? 5 : 6; continue; }
            if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; continue; }
            if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; continue; }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

#define NEWS_MAX 4
static struct {
    char title[110];
    char source[32];
} news[NEWS_MAX];

static int read_news(void) {
    char *body = ls_http_get("https://news.google.com/rss?hl=es-419&gl=CO&ceid=CO:es-419", true);
    if (!body) return 0;
    int n = 0;
    char *p = body;
    while (n < NEWS_MAX && (p = strstr(p, "<item>"))) {
        char *start = strstr(p, "<title>");
        char *end = start ? strstr(start, "</title>") : NULL;
        if (!start || !end) break;
        start += 7;
        if (strncmp(start, "<![CDATA[", 9) == 0) start += 9;
        size_t len = (size_t)(end - start);
        if (len >= 3 && strncmp(start + len - 3, "]]>", 3) == 0) len -= 3;
        char raw[200];
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        memcpy(raw, start, len);
        raw[len] = '\0';
        unescape_entities(raw);
        char *dash = strrchr(raw, '-');
        news[n].source[0] = '\0';
        if (dash && dash > raw + 3 && dash[-1] == ' ' && dash[1] == ' ') {
            strlcpy(news[n].source, dash + 2, sizeof(news[n].source));
            dash[-1] = '\0';
        }
        strlcpy(news[n].title, raw, sizeof(news[n].title));
        n++;
        p = end;
    }
    heap_caps_free(body);
    return n;
}

static bool try_news(const char *text, local_answer_t *a) {
    if (!(has(text, "noticias") || has(text, "noticia") || has(text, "titulares") || strstr(text, " que paso hoy ") ||
          strstr(text, " que esta pasando "))) {
        return false;
    }
    int n = read_news();
    if (n == 0) {
        ls_say(a, CLIP_SIN_CONEXION);
        ls_finish(a, "No tengo conexión en este momento.", NULL);
        return true;
    }
    char *json = heap_caps_malloc(1500, MALLOC_CAP_SPIRAM);
    if (json) {
        size_t used = (size_t)snprintf(json, 1500, "{\"type\":\"headlines\",\"title\":\"Noticias\",\"footer\":\"Google Noticias\",\"items\":[");
        for (int i = 0; i < n; i++) {
            char t[120], s[40];
            json_text(news[i].title, t, sizeof(t));
            json_text(news[i].source, s, sizeof(s));
            used += (size_t)snprintf(json + used, 1500 - used, "%s{\"title\":\"%s\",\"source\":\"%s\"}", i ? "," : "", t, s);
        }
        snprintf(json + used, 1500 - used, "]}");
    }
    char spoken[420] = "Estas son las noticias principales de Colombia. ";
    static const char *const ORDER[] = {"Primera", "Segunda", "Tercera"};
    for (int i = 0; i < n && i < 3; i++) {
        char piece[140];
        snprintf(piece, sizeof(piece), "%s: %s. ", ORDER[i], news[i].title);
        if (strlen(spoken) + strlen(piece) < sizeof(spoken)) strlcat(spoken, piece, sizeof(spoken));
    }
    ls_speak(a, spoken);
    ls_finish(a, "Estas son las noticias principales.", "headlines");
    a->json = json;
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// ---------------------------------------------------------------------------------------- Wikipedia
static void url_encode(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < size; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') out[o++] = (char)*p;
        else if (*p == ' ') out[o++] = '_';
        else o += (size_t)snprintf(out + o, size - o, "%%%02X", *p);
    }
    out[o] = '\0';
}

// Words of a sentence, without the punctuation around them
static int split_words(const char *text, const char **start, int *length, int max) {
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        while (*p && (*p == ' ' || strchr(",.;:!?¿¡\"'", *p))) p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ' && !strchr(",.;:!?\"", *p)) p++;
        start[n] = s;
        length[n] = (int)(p - s);
        n++;
    }
    return n;
}

static bool try_wikipedia(const char *original, const char *text, local_answer_t *a) {
    static const char *const TRIGGERS[] = {" quien fue ", " quien es ", " quien era ", " quienes eran ", " quienes son ", " que es ", " que fue ",
                                           " que son ", " que significa ", " cuentame de ", " cuentame sobre ", " cuentame acerca de ",
                                           " hablame de ", " hablame sobre ", " hablame acerca de ", " dime algo de ", " dime algo sobre ",
                                           " dime de ", " dime sobre ", " busca ", " wikipedia ", NULL};
    const char *subject = NULL;
    int skipped_words = 0;
    for (int i = 0; TRIGGERS[i] && !subject; i++) {
        const char *at = strstr(text, TRIGGERS[i]);
        if (at) {
            subject = at + strlen(TRIGGERS[i]);
            // how many words come before the subject, to find the same place in the original sentence
            for (const char *c = text; c < subject - 1; c++) {
                if (*c == ' ' && c[1] != ' ' && c[1] != '\0') skipped_words++;
            }
        }
    }
    if (!subject || !subject[0] || (subject[0] == ' ' && !subject[1])) return false;
    static const char *const NOT_FOR_WIKI[] = {"musica", "emisora", "radio", "cancion", "actualizacion", "firmware", "correo", "correos",
                                               "calendario", "agenda", "alarma", "temporizador", "lista", "hora", "dia", "clima", NULL};
    for (int i = 0; NOT_FOR_WIKI[i]; i++) {
        if (has(subject - 1, NOT_FOR_WIKI[i])) return false;
    }

    // The subject with the accents that were heard, if both versions split into the same words
    static char query[100];
    query[0] = '\0';
    const char *ow[24], *nw[24];
    int ol[24], nl[24];
    int on = split_words(original, ow, ol, 24), nn = split_words(text, nw, nl, 24);
    if (on == nn && skipped_words <= on) {
        for (int i = skipped_words; i < on; i++) {
            if (query[0]) strlcat(query, " ", sizeof(query));
            size_t at = strlen(query);
            size_t take = (size_t)ol[i] < sizeof(query) - at - 1 ? (size_t)ol[i] : sizeof(query) - at - 1;
            memcpy(query + at, ow[i], take);
            query[at + take] = '\0';
        }
    } else {
        strlcpy(query, subject, sizeof(query));
        for (int n = (int)strlen(query) - 1; n >= 0 && query[n] == ' '; n--) query[n] = '\0';
    }
    // leading articles are not part of the title
    static const char *const ARTICLES[] = {"el ", "la ", "los ", "las ", "un ", "una ", "de ", "del ", NULL};
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; ARTICLES[i]; i++) {
            size_t n = strlen(ARTICLES[i]);
            if (strncasecmp(query, ARTICLES[i], n) == 0) {
                memmove(query, query + n, strlen(query + n) + 1);
                again = true;
            }
        }
    }
    if (!query[0]) return false;

    static char encoded[320], url[420];
    url_encode(query, encoded, sizeof(encoded));
    for (int i = 0; encoded[i]; i++) {
        if (encoded[i] == '_') encoded[i] = '+';
    }
    snprintf(url, sizeof(url), "https://es.wikipedia.org/w/api.php?action=opensearch&search=%s&limit=3&namespace=0&format=json", encoded);
    char *body = ls_http_get(url, true);
    if (!body) {
        ls_say(a, CLIP_SIN_CONEXION);
        ls_finish(a, "No tengo conexión en este momento.", NULL);
        return true;
    }
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    cJSON *titles = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 1) : NULL;
    char *answer_json = NULL;
    char spoken[420] = "", title_out[100] = "";
    for (int i = 0; titles && i < cJSON_GetArraySize(titles) && !spoken[0]; i++) {
        cJSON *title = cJSON_GetArrayItem(titles, i);
        if (!cJSON_IsString(title)) continue;
        static char page[320];
        url_encode(title->valuestring, page, sizeof(page));
        snprintf(url, sizeof(url), "https://es.wikipedia.org/api/rest_v1/page/summary/%s", page);
        char *summary_body = ls_http_get(url, true);
        if (!summary_body) continue;
        cJSON *summary = cJSON_Parse(summary_body);
        heap_caps_free(summary_body);
        cJSON *kind = summary ? cJSON_GetObjectItem(summary, "type") : NULL;
        cJSON *extract = summary ? cJSON_GetObjectItem(summary, "extract") : NULL;
        if (cJSON_IsString(kind) && strcmp(kind->valuestring, "standard") == 0 && cJSON_IsString(extract) && extract->valuestring[0]) {
            first_sentences(extract->valuestring, 380, spoken, sizeof(spoken));
            strlcpy(title_out, title->valuestring, sizeof(title_out));
            char shown[420];
            first_sentences(extract->valuestring, 330, shown, sizeof(shown));
            answer_json = ls_article_json(title_out, shown, "Wikipedia");
        }
        cJSON_Delete(summary);
    }
    cJSON_Delete(root);
    if (!spoken[0]) {
        ls_finish(a, "No encontré eso en Wikipedia.", NULL);
        ls_say(a, CLIP_NO_ENCONTRE_WIKI);
        return true;
    }
    ESP_LOGI(TAG, "Wikipedia: %s", title_out);
    ls_speak(a, spoken);
    char reply[96];
    snprintf(reply, sizeof(reply), "%s", title_out);
    ls_finish(a, reply, "article");
    a->json = answer_json;
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// ---------------------------------------------------------------------------------- jokes from the internet
// A random joke from JokeAPI (free, no key; safe jokes only), translated into Spanish by MyMemory (free, no key).
// If either service does not answer, one of the jokes stored on the board is told instead.
bool ls_translate(const char *from, const char *to, const char *english, char *out, size_t size) {
    static char encoded[400], url[560];
    url_encode(english, encoded, sizeof(encoded));
    for (int i = 0; encoded[i]; i++) {
        if (encoded[i] == '_') encoded[i] = '+';  // url_encode writes a space as an underscore
    }
    snprintf(url, sizeof(url), "https://api.mymemory.translated.net/get?q=%s&langpair=%s%%7C%s", encoded, from, to);
    char *body = ls_http_get(url, true);
    if (!body) return false;
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    cJSON *data = root ? cJSON_GetObjectItem(root, "responseData") : NULL;
    cJSON *translated = data ? cJSON_GetObjectItem(data, "translatedText") : NULL;
    bool ok = cJSON_IsString(translated) && translated->valuestring[0] && !strstr(translated->valuestring, "MYMEMORY");
    if (ok) {
        strlcpy(out, translated->valuestring, size);
        unescape_entities(out);
    }
    cJSON_Delete(root);
    return ok;
}

static bool fetch_online_joke(char *setup, size_t setup_size, char *punchline, size_t punchline_size) {
    char *body = ls_http_get("https://v2.jokeapi.dev/joke/Misc,Pun?blacklistFlags=nsfw,religious,political,racist,sexist,explicit&safe-mode&type=twopart&lang=en", true);
    if (!body) return false;
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    cJSON *first = root ? cJSON_GetObjectItem(root, "setup") : NULL;
    cJSON *second = root ? cJSON_GetObjectItem(root, "delivery") : NULL;
    bool ok = false;
    if (cJSON_IsString(first) && cJSON_IsString(second) && strlen(first->valuestring) < 200 && strlen(second->valuestring) < 200) {
        // One translation for both parts (each secure connection costs about 1.4 s on the board), split again at the marker that survives the translation
        if (strlen(first->valuestring) + strlen(second->valuestring) < 150) {
            static char joined[340], both[420];
            snprintf(joined, sizeof(joined), "%s ||| %s", first->valuestring, second->valuestring);
            if (ls_translate("en", "es", joined, both, sizeof(both))) {
                char *cut = strstr(both, "|||");
                if (cut) {
                    char *end = cut;
                    while (end > both && end[-1] == ' ') end--;
                    *end = ' ';
                    cut += 3;
                    while (*cut == ' ') cut++;
                    strlcpy(setup, both, setup_size);
                    strlcpy(punchline, cut, punchline_size);
                    ok = setup[0] && punchline[0];
                }
            }
        }
        if (!ok) {  // the fallback: each part on its own
            ok = ls_translate("en", "es", first->valuestring, setup, setup_size) && ls_translate("en", "es", second->valuestring, punchline, punchline_size);
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool try_joke(const char *text, local_answer_t *a) {
    if (!(has(text, "chiste") || has(text, "chistes") || has(text, "hazme reir") || strstr(text, " algo gracioso "))) return false;
    char setup[220], punchline[220];
    bool online = fetch_online_joke(setup, sizeof(setup), punchline, sizeof(punchline));
    if (!online) {
        ESP_LOGW(TAG, "No joke from the internet, telling a stored one");
        int index;
        do {
            index = (int)(esp_random() % (sizeof(JOKES) / sizeof(JOKES[0])));
        } while (index == last_joke);
        last_joke = index;
        strlcpy(setup, JOKES[index][0], sizeof(setup));
        strlcpy(punchline, JOKES[index][1], sizeof(punchline));
    }
    char spoken[420], shown[420];
    snprintf(spoken, sizeof(spoken), "%s ... %s", setup, punchline);
    snprintf(shown, sizeof(shown), "%s\n\n%s", setup, punchline);
    ls_speak(a, spoken);
    ls_finish(a, "Un chiste.", "article");
    a->json = ls_article_json("Chiste", shown, online ? "De internet" : "");
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// ------------------------------------------------------------------------------------------ entry
bool local_online_try(const char *original, const char *text, local_answer_t *a) {
    if (try_joke(text, a)) return true;
    if (try_news(text, a)) return true;
    return try_wikipedia(original, text, a);
}

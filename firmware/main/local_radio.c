#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "music_player.h"
#include "nvs.h"
#include "voice_clips.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Radio and music the board starts by itself: it asks the free Radio Browser directory for stations (MP3 only,
// because the board decodes MP3), plays the stream directly, and plays the MP3 files on the microSD card.

static const char *TAG = "RADIO";

#define MAX_STATIONS 8
#define COUNTRY "CO"

typedef struct {
    char name[64];
    char url[200];
    char detail[48];
} station_t;

static station_t stations[MAX_STATIONS];
static int station_count = 0, station_index = 0;
static bool card_mode = false;

static int failures = 0;  // stations that did not play since the last request

// The station did not play: the next one from the search (NULL when all were tried). Also tells the music
// screen which station is on now.
const char *local_radio_failover(void) {
    if (card_mode || station_count < 2 || ++failures >= station_count) return NULL;
    station_index = (station_index + 1) % station_count;
    ESP_LOGW(TAG, "Station did not play, trying %s", stations[station_index].name);
    music_set_now_playing(stations[station_index].name, stations[station_index].detail);
    return stations[station_index].url;
}

static const char *const MIRRORS[] = {"de1.api.radio-browser.info", "all.api.radio-browser.info", "de2.api.radio-browser.info", "fi1.api.radio-browser.info", "nl1.api.radio-browser.info", NULL};

static const struct { const char *spoken, *tag; } GENRES[] = {
    {"salsa", "salsa"}, {"vallenato", "vallenato"}, {"reggaeton", "reggaeton"}, {"rock", "rock"}, {"pop", "pop"},
    {"baladas", "balada"}, {"balada", "balada"}, {"clasica", "classical"}, {"jazz", "jazz"}, {"electronica", "electronic"},
    {"merengue", "merengue"}, {"bachata", "bachata"}, {"cumbia", "cumbia"}, {"tango", "tango"}, {"metal", "metal"},
    {"rap", "rap"}, {"boleros", "bolero"}, {"bolero", "bolero"}, {"tropical", "tropical"}, {"urbana", "urban"},
    {"noticias", "news"}, {"deportes", "sports"}, {"ranchera", "ranchera"}, {"mariachi", "mariachi"}, {"popular", "popular"}};

static const char *const TRIGGERS[] = {"pon", "ponme", "poner", "toca", "tocar", "reproduce", "reproducir", "quiero", "escuchar", "oir",
                                       "escucha", "puedes", "podrias", "pone", NULL};
static const char *const FILLERS[] = {"musica", "radio", "emisora", "la", "el", "de", "una", "algo", "por", "favor", "me", "en", "un",
                                      "poco", "las", "hola", "bug", NULL};
static const char *const KNOWN[] = {"tropicana", "olimpica", "caracol", "rcn", "blu", "fm", NULL};

static bool in_list(const char *w, const char *const *list) {
    for (; *list; list++) {
        if (strcmp(w, *list) == 0) return true;
    }
    return false;
}

static bool has(const char *text, const char *word) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

static void url_encode(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < size; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') {
            out[o++] = (char)*p;
        } else {
            o += (size_t)snprintf(out + o, size - o, "%%%02X", *p);
        }
    }
    out[o] = '\0';
}

static bool search(const char *query_extra, bool with_country) {
    static char url[420];
    for (int round = 0; round < 2; round++)
    for (int i = 0; MIRRORS[i]; i++) {
        snprintf(url, sizeof(url),
                 "https://%s/json/stations/search?hidebroken=true&order=clickcount&reverse=true&limit=12&codec=MP3%s%s%s", MIRRORS[i],
                 with_country ? "&countrycode=" COUNTRY : "", query_extra[0] ? "&" : "", query_extra);
        char *body = ls_http_get(url, true);
        if (!body) continue;
        cJSON *root = cJSON_Parse(body);
        heap_caps_free(body);
        if (!cJSON_IsArray(root)) {
            cJSON_Delete(root);
            continue;
        }
        station_count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, root) {
            cJSON *name = cJSON_GetObjectItem(item, "name"), *link = cJSON_GetObjectItem(item, "url_resolved");
            cJSON *state = cJSON_GetObjectItem(item, "state");
            if (!cJSON_IsString(name) || !cJSON_IsString(link) || !link->valuestring[0] || strlen(link->valuestring) >= sizeof(stations[0].url)) continue;
            station_t *s = &stations[station_count];
            strlcpy(s->name, name->valuestring, sizeof(s->name));
            for (char *q = s->name; *q; q++) {
                if (*q == '"' || *q == '\\') *q = ' ';
            }
            // trailing spaces in the directory's names
            for (int n = (int)strlen(s->name) - 1; n >= 0 && s->name[n] == ' '; n--) s->name[n] = '\0';
            strlcpy(s->url, link->valuestring, sizeof(s->url));
            strlcpy(s->detail, cJSON_IsString(state) ? state->valuestring : "", sizeof(s->detail));
            if (++station_count >= MAX_STATIONS) break;
        }
        cJSON_Delete(root);
        return true;  // the directory answered (the list may be empty)
    }
    return false;
}

// What the screen and the player need once the answer has been spoken
const char *local_radio_url(void) {
    return card_mode ? "sd:" : stations[station_index].url;
}

char *local_radio_screen_json(void) {
    char *json = heap_caps_malloc(260, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    if (card_mode) {
        snprintf(json, 260, "{\"type\":\"music\",\"source\":\"Mi música\",\"station\":\"Tarjeta SD\",\"detail\":\"\"}");
    } else {
        snprintf(json, 260, "{\"type\":\"music\",\"source\":\"Radio\",\"station\":\"%s\",\"detail\":\"%s\"}", stations[station_index].name,
                 stations[station_index].detail);
    }
    return json;
}

static void say_playing(local_answer_t *a, const char *reply) {
    ls_say(a, CLIP_PONGO_RADIO);
    ls_finish(a, reply, "music_local");
}

// ------------------------------------------------------------------------------------------ favourites
#define MAX_FAVORITES 5
static station_t favorites[MAX_FAVORITES];
static int favorite_count = -1;  // -1: not read from memory yet

static void load_favorites(void) {
    if (favorite_count >= 0) return;
    favorite_count = 0;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(favorites);
        if (nvs_get_blob(handle, "favs", favorites, &size) == ESP_OK) favorite_count = (int)(size / sizeof(station_t));
        nvs_close(handle);
    }
    if (favorite_count > MAX_FAVORITES) favorite_count = MAX_FAVORITES;
}

static void save_favorites(void) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "favs", favorites, (size_t)favorite_count * sizeof(station_t));
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool try_favorites(const char *text, local_answer_t *a) {
    bool favorite_word = has(text, "favorita") || has(text, "favoritas") || has(text, "favorito") || has(text, "favoritos");
    if (!favorite_word) return false;
    load_favorites();
    bool save = has(text, "guarda") || has(text, "guardar") || has(text, "agrega") || has(text, "anade") || has(text, "agregar");
    bool remove = has(text, "quita") || has(text, "quitar") || has(text, "borra") || has(text, "elimina");
    if (save || remove) {
        if (card_mode || station_count == 0 || !music_wanted()) {
            ls_say(a, CLIP_NADA_QUE_GUARDAR);
            ls_finish(a, "No hay ninguna emisora para guardar.", NULL);
            return true;
        }
        const station_t *now = &stations[station_index];
        int found = -1;
        for (int i = 0; i < favorite_count; i++) {
            if (strcmp(favorites[i].url, now->url) == 0) found = i;
        }
        if (remove) {
            if (found < 0) {
                ls_say(a, CLIP_NADA_QUE_GUARDAR);
                ls_finish(a, "Esa emisora no está en tus favoritas.", NULL);
                return true;
            }
            for (int i = found; i + 1 < favorite_count; i++) favorites[i] = favorites[i + 1];
            favorite_count--;
            save_favorites();
            ls_say(a, CLIP_FAV_QUITADA);
            ls_finish(a, "Listo, quité la emisora de tus favoritas.", NULL);
            return true;
        }
        if (found >= 0) {
            ls_say(a, CLIP_FAV_YA);
            ls_finish(a, "Esa emisora ya está en tus favoritas.", NULL);
            return true;
        }
        if (favorite_count == MAX_FAVORITES) {  // the oldest goes
            for (int i = 0; i + 1 < favorite_count; i++) favorites[i] = favorites[i + 1];
            favorite_count--;
        }
        favorites[favorite_count++] = *now;
        save_favorites();
        ls_say(a, CLIP_FAV_GUARDADA);
        char reply[96];
        snprintf(reply, sizeof(reply), "Guardé %s en tus favoritas.", now->name);
        ls_finish(a, reply, NULL);
        return true;
    }
    // "pon mis favoritas": the favourites become the list "otra emisora" goes through
    if (favorite_count == 0) {
        ls_say(a, CLIP_FAV_NINGUNA);
        ls_finish(a, "Todavía no tienes emisoras favoritas.", NULL);
        return true;
    }
    memcpy(stations, favorites, (size_t)favorite_count * sizeof(station_t));
    station_count = favorite_count;
    station_index = 0;
    failures = 0;
    card_mode = false;
    char reply[96];
    snprintf(reply, sizeof(reply), "Poniendo %s.", stations[0].name);
    say_playing(a, reply);
    return true;
}

bool local_radio_try(const char *original, const char *text, local_answer_t *a) {
    if (try_favorites(text, a)) return true;
    bool music_word = has(text, "musica") || has(text, "radio") || has(text, "emisora");

    // Next song or previous song of the card
    bool next_song = (has(text, "siguiente") || has(text, "otra") || has(text, "proxima") || has(text, "salta") || has(text, "pasa")) &&
                     (has(text, "cancion") || has(text, "tema"));
    bool previous_song = (has(text, "anterior") || has(text, "previa")) && (has(text, "cancion") || has(text, "tema"));
    if ((next_song || previous_song) && music_card_playing()) {
        music_next_song(previous_song ? -1 : 1);
        ls_say(a, CLIP_LISTO);
        ls_finish(a, "Listo.", NULL);
        return true;
    }

    // Another station from the last search
    if ((has(text, "otra") || has(text, "siguiente") || has(text, "cambia") || has(text, "cambiar")) &&
        (has(text, "emisora") || has(text, "radio") || has(text, "estacion")) && station_count > 1 && !card_mode) {
        station_index = (station_index + 1) % station_count;
        failures = 0;
        say_playing(a, "Poniendo otra emisora.");
        return true;
    }

    // The music on the card
    if (has(text, "mi musica") || has(text, "mis canciones") || has(text, "mi tarjeta") || has(text, "musica de la tarjeta") ||
        has(text, "musica de mi tarjeta")) {
        if (!music_card_has_songs()) return false;  // the PC's library may have it
        card_mode = true;
        ls_say(a, CLIP_PONGO_TARJETA);
        ls_finish(a, "Poniendo tu música.", "music_local");
        return true;
    }

    // A station or a kind of music
    static char copy[200];
    strlcpy(copy, text, sizeof(copy));
    bool trigger = false, genre = false, known = false;
    static char request[100];
    request[0] = '\0';
    const char *tag = NULL;
    char *save = NULL;
    for (char *w = strtok_r(copy, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
        if (in_list(w, TRIGGERS)) {
            trigger = true;
            continue;
        }
        for (size_t i = 0; i < sizeof(GENRES) / sizeof(GENRES[0]); i++) {
            if (strcmp(w, GENRES[i].spoken) == 0) {
                genre = true;
                tag = GENRES[i].tag;
            }
        }
        if (in_list(w, KNOWN)) known = true;
        if (!in_list(w, FILLERS)) {
            if (request[0]) strlcat(request, " ", sizeof(request));
            strlcat(request, w, sizeof(request));
        }
    }
    if (!trigger || !(music_word || genre || known || has(text, "escuchar") || has(text, "oir"))) return false;

    static char extra[160];
    extra[0] = '\0';
    if (tag) {
        snprintf(extra, sizeof(extra), "tag=%s", tag);
    } else if (request[0]) {
        // The same words with the accents that were heard, which the directory's name search needs ("Olímpica")
        static char name[100];
        name[0] = '\0';
        static char encoded[160];
        static char original_copy[200];
        strlcpy(original_copy, original, sizeof(original_copy));
        char *save2 = NULL;
        for (char *w = strtok_r(original_copy, " ", &save2); w; w = strtok_r(NULL, " ", &save2)) {
            char plain[48] = "";
            size_t n = 0;
            for (const char *p = w; *p && n + 1 < sizeof(plain); p++) {
                if (isalnum((unsigned char)*p) || (unsigned char)*p >= 0x80) plain[n++] = *p;
            }
            plain[n] = '\0';
            if (!n) continue;
            // is this word one of the trigger or filler words (compared without accents)? keep it only if not
            char lower[48];
            size_t m = 0;
            for (size_t k = 0; plain[k] && m + 1 < sizeof(lower); k++) {
                unsigned char c = (unsigned char)plain[k];
                if (c < 0x80) lower[m++] = (char)tolower(c);
            }
            lower[m] = '\0';
            if (in_list(lower, TRIGGERS) || in_list(lower, FILLERS)) continue;
            if (name[0]) strlcat(name, " ", sizeof(name));
            strlcat(name, plain, sizeof(name));
        }
        url_encode(name[0] ? name : request, encoded, sizeof(encoded));
        snprintf(extra, sizeof(extra), "name=%s", encoded);
    }
    card_mode = false;
    if (!search(extra, true)) return false;  // no internet: the PC may still find it
    if (station_count == 0 && extra[0]) {
        if (!search(extra, false)) return false;
    }
    if (station_count == 0) {
        ls_say(a, CLIP_NO_EMISORA);
        ls_finish(a, "No encontré esa emisora.", NULL);
        return true;
    }
    station_index = 0;
    failures = 0;
    ESP_LOGI(TAG, "%d stations, playing %s", station_count, stations[0].name);
    char reply[96];
    snprintf(reply, sizeof(reply), "Poniendo %s.", stations[0].name);
    say_playing(a, reply);
    return true;
}

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "nvs.h"
#include "voice_clips.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The weather, fetched by the board itself from Open-Meteo (free, no key). The place comes from the internet
// connection (ip-api.com) and is remembered, so an old forecast and the place still work when the internet is gone.

static const char *TAG = "WEATHER";

#define CACHE_SECONDS (3 * 3600)   // an older forecast is not used
#define FRESH_SECONDS 600          // a forecast this young is reused without asking again

typedef struct {
    double lat, lon;
    char city[32];
} place_t;

typedef struct {
    time_t fetched;
    int temp, high, low, rain, code;
    int day_high[3], day_low[3], day_code[3], day_weekday[3];  // the next three days
} forecast_t;

static place_t place;
static bool place_known = false;
static forecast_t forecast;

// ---- the HTTP helper: GET a small text body
char *ls_http_get_len(const char *url, bool secure, size_t *length) {
    esp_http_client_config_t config = {.url = url, .timeout_ms = 8000, .buffer_size = 1024, .user_agent = "Mozilla/5.0 (BugVoiceAssistant)"};
    if (secure) config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    char *body = NULL;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) == 200) {
            size_t cap = 4096, len = 0;
            body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
            while (body) {
                int got = esp_http_client_read(client, body + len, (int)(cap - len - 1));
                if (got <= 0) break;
                len += (size_t)got;
                if (len + 1 >= cap) {
                    cap *= 2;
                    char *bigger = heap_caps_realloc(body, cap, MALLOC_CAP_SPIRAM);
                    if (!bigger) break;
                    body = bigger;
                }
            }
            if (body) body[len] = '\0';
            if (length) *length = len;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return body;
}

char *ls_http_get(const char *url, bool secure) {
    return ls_http_get_len(url, secure, NULL);
}

// ---- where the board is
static void save_place(void) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "place", &place, sizeof(place));
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool find_place(void) {
    if (place_known) return true;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(place);
        if (nvs_get_blob(handle, "place", &place, &size) == ESP_OK && size == sizeof(place)) place_known = true;
        nvs_close(handle);
    }
    char *body = ls_http_get("http://ip-api.com/json/?fields=status,city,lat,lon", false);
    if (body) {
        cJSON *root = cJSON_Parse(body);
        heap_caps_free(body);
        cJSON *status = root ? cJSON_GetObjectItem(root, "status") : NULL;
        if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
            cJSON *lat = cJSON_GetObjectItem(root, "lat"), *lon = cJSON_GetObjectItem(root, "lon"), *city = cJSON_GetObjectItem(root, "city");
            if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
                place.lat = lat->valuedouble;
                place.lon = lon->valuedouble;
                strlcpy(place.city, cJSON_IsString(city) ? city->valuestring : "", sizeof(place.city));
                place_known = true;
                save_place();
            }
        }
        cJSON_Delete(root);
    }
    return place_known;
}

// ---- the forecast
static bool fetch_forecast(void) {
    char url[300];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=4",
             place.lat, place.lon);
    char *body = ls_http_get(url, true);
    if (!body) return false;
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (!root) return false;
    bool ok = false;
    cJSON *current = cJSON_GetObjectItem(root, "current"), *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *temp = current ? cJSON_GetObjectItem(current, "temperature_2m") : NULL;
    cJSON *code = current ? cJSON_GetObjectItem(current, "weather_code") : NULL;
    cJSON *codes = daily ? cJSON_GetObjectItem(daily, "weather_code") : NULL;
    cJSON *highs = daily ? cJSON_GetObjectItem(daily, "temperature_2m_max") : NULL;
    cJSON *lows = daily ? cJSON_GetObjectItem(daily, "temperature_2m_min") : NULL;
    cJSON *rains = daily ? cJSON_GetObjectItem(daily, "precipitation_probability_max") : NULL;
    cJSON *dates = daily ? cJSON_GetObjectItem(daily, "time") : NULL;
    if (cJSON_IsNumber(temp) && cJSON_IsNumber(code) && cJSON_GetArraySize(codes) >= 4 && cJSON_GetArraySize(highs) >= 4 &&
        cJSON_GetArraySize(lows) >= 4 && cJSON_GetArraySize(dates) >= 4) {
        forecast.temp = (int)(temp->valuedouble + (temp->valuedouble < 0 ? -0.5 : 0.5));
        forecast.code = code->valueint;
        forecast.high = (int)(cJSON_GetArrayItem(highs, 0)->valuedouble + 0.5);
        forecast.low = (int)(cJSON_GetArrayItem(lows, 0)->valuedouble + 0.5);
        cJSON *rain = cJSON_GetArrayItem(rains, 0);
        forecast.rain = cJSON_IsNumber(rain) ? rain->valueint : 0;
        for (int i = 0; i < 3; i++) {
            forecast.day_high[i] = (int)(cJSON_GetArrayItem(highs, i + 1)->valuedouble + 0.5);
            forecast.day_low[i] = (int)(cJSON_GetArrayItem(lows, i + 1)->valuedouble + 0.5);
            forecast.day_code[i] = cJSON_GetArrayItem(codes, i + 1)->valueint;
            int y = 0, m = 0, d = 0;
            sscanf(cJSON_GetArrayItem(dates, i + 1)->valuestring, "%d-%d-%d", &y, &m, &d);
            struct tm t = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_hour = 12};
            mktime(&t);
            forecast.day_weekday[i] = (t.tm_wday + 6) % 7;  // Monday = 0
        }
        forecast.fetched = time(NULL);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

// WMO weather code -> clip, the text and the icon the weather screen draws
static void describe(int code, int *clip, const char **text, const char **icon) {
    if (code == 0) { *clip = CLIP_COND_DESPEJADO; *text = "Despejado"; *icon = "clear"; }
    else if (code == 1) { *clip = CLIP_COND_MAYORMENTE; *text = "Mayormente despejado"; *icon = "clear"; }
    else if (code == 2) { *clip = CLIP_COND_PARCIAL; *text = "Parcialmente nublado"; *icon = "partly"; }
    else if (code == 45 || code == 48) { *clip = CLIP_COND_NIEBLA; *text = "Niebla"; *icon = "fog"; }
    else if (code >= 51 && code <= 57) { *clip = CLIP_COND_LLOVIZNA; *text = "Llovizna"; *icon = "rain"; }
    else if ((code >= 61 && code <= 67)) { *clip = CLIP_COND_LLUVIA; *text = "Lluvia"; *icon = "rain"; }
    else if (code >= 80 && code <= 82) { *clip = CLIP_COND_AGUACEROS; *text = "Aguaceros"; *icon = "rain"; }
    else if (code >= 95 && code <= 99) { *clip = CLIP_COND_TORMENTA; *text = "Tormenta eléctrica"; *icon = "storm"; }
    else if ((code >= 71 && code <= 77) || code == 85 || code == 86) { *clip = CLIP_COND_NIEVE; *text = "Nieve"; *icon = "cloudy"; }
    else { *clip = CLIP_COND_NUBLADO; *text = "Nublado"; *icon = "cloudy"; }
}

static char *build_json(void) {
    static const char *const SHORT_DAYS[] = {"Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom"};
    char *json = heap_caps_malloc(900, MALLOC_CAP_SPIRAM);
    if (!json) return NULL;
    int clip;
    const char *text, *icon;
    describe(forecast.code, &clip, &text, &icon);
    size_t used = snprintf(json, 900, "{\"type\":\"weather\",\"city\":\"%s\",\"temp\":%d,\"text\":\"%s\",\"icon\":\"%s\",\"high\":%d,\"low\":%d,\"rain\":%d,\"days\":[",
                           place.city, forecast.temp, text, icon, forecast.high, forecast.low, forecast.rain);
    for (int i = 0; i < 3; i++) {
        const char *day_text, *day_icon;
        int day_clip;
        describe(forecast.day_code[i], &day_clip, &day_text, &day_icon);
        used += snprintf(json + used, 900 - used, "%s{\"day\":\"%s\",\"icon\":\"%s\",\"high\":%d,\"low\":%d}", i ? "," : "",
                         SHORT_DAYS[forecast.day_weekday[i]], day_icon, forecast.day_high[i], forecast.day_low[i]);
    }
    snprintf(json + used, 900 - used, "]}");
    return json;
}

// ---- the skill
static bool has(const char *text, const char *word) {
    char padded[32];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// A sentence about the weather for the morning briefing; false without internet or an old forecast
bool local_weather_brief(char *out, size_t size) {
    time_t now = time(NULL);
    bool fresh = forecast.fetched && now - forecast.fetched < FRESH_SECONDS;
    if (!fresh && !(find_place() && fetch_forecast()) && !(forecast.fetched && now - forecast.fetched < CACHE_SECONDS)) return false;
    int clip;
    const char *condition, *icon;
    describe(forecast.code, &clip, &condition, &icon);
    snprintf(out, size, "Afuera hay %d grados, %s. Hoy la máxima será de %d y la mínima de %d.", forecast.temp, condition, forecast.high, forecast.low);
    return true;
}

bool local_weather_try(const char *text, local_answer_t *a) {
    if (!(has(text, "clima") || has(text, "tiempo") || has(text, "llover") || has(text, "llueve") || has(text, "temperatura") ||
          has(text, "pronostico"))) {
        return false;
    }
    // A named city ("el clima en Cali") or another day is for the PC: it knows how to look those up
    static const char *const allowed[] = {"como", "esta", "el", "la", "clima", "tiempo", "hoy", "que", "hace", "va", "a", "llover", "llueve",
                                          "temperatura", "pronostico", "tal", "dime", "me", "cual", "es", "afuera", "ahora", "por", "favor",
                                          "hola", "bug", "un", "poco", "cuanta", "cuantos", "grados", "hay", "tengo", NULL};
    char copy[200];
    strlcpy(copy, text, sizeof(copy));
    char *save = NULL;
    for (char *w = strtok_r(copy, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
        bool ok = false;
        for (int i = 0; allowed[i] && !ok; i++) ok = strcmp(w, allowed[i]) == 0;
        if (!ok) return false;
    }

    time_t now = time(NULL);
    bool fresh = forecast.fetched && now - forecast.fetched < FRESH_SECONDS;
    if (!fresh) {
        bool fetched = find_place() && fetch_forecast();
        if (!fetched && !(forecast.fetched && now - forecast.fetched < CACHE_SECONDS)) {
            ls_say(a, CLIP_SIN_CONEXION);
            ls_finish(a, "No tengo conexión en este momento.", NULL);
            return true;
        }
        if (!fetched) ESP_LOGW(TAG, "No internet: using the forecast from a while ago");
    }
    int clip;
    const char *condition, *icon;
    describe(forecast.code, &clip, &condition, &icon);
    ls_say(a, CLIP_AFUERA_HAY);
    ls_say_number(a, forecast.temp < 0 ? 0 : forecast.temp);
    ls_say(a, CLIP_GRADOS);
    ls_say(a, clip);
    ls_say(a, CLIP_HOY_MAXIMA);
    ls_say_number(a, forecast.high < 0 ? 0 : forecast.high);
    ls_say(a, CLIP_Y_MINIMA);
    ls_say_number(a, forecast.low < 0 ? 0 : forecast.low);
    char reply[160];
    snprintf(reply, sizeof(reply), "Afuera hay %d grados. %s. Hoy la máxima será de %d y la mínima de %d, con %d por ciento de probabilidad de lluvia.",
             forecast.temp, condition, forecast.high, forecast.low, forecast.rain);
    ls_finish(a, reply, "weather");
    a->json = build_json();
    if (!a->json) a->screen[0] = '\0';
    return true;
}

// The smart home by voice, over MQTT: "enciende la luz de la sala", "apaga el ventilador", "¿está encendida la luz del cuarto?".
//
// The devices are listed in a text file on the microSD card, /sd/home/devices.txt, one per line:
//
//     name | another name => command topic | state topic
//     luz de la sala | lámpara de la sala => casa/sala/luz/set | casa/sala/luz/state
//
// The board sends ON or OFF to the command topic (and reads the retained message of the state topic to answer "¿está encendida?").
// The broker is the MQTT address kept in the board's memory (BUGCFG {"mqtt":"mqtt://user:password@host:1883"}); without one,
// the PC that runs the Bug server is tried (port 1883). Only devices written in the file are touched.
#include "local_context.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[64];     // the first name, as written (for the answer)
    char command[96];  // topic that receives ON / OFF
    char state[96];    // topic that holds ON / OFF (may be empty)
} home_device_t;

typedef enum { HOME_NONE, HOME_ON, HOME_OFF, HOME_STATE } home_action_t;

static bool has(const char *text, const char *word) {
    char padded[48];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// What the sentence asks for (it is normalized: lowercase, no accents)
home_action_t home_parse(const char *sentence) {
    static const char *const ask[] = {"esta encendida", "esta encendido", "esta prendida", "esta prendido", "esta apagada", "esta apagado",
                                      "estan encendidas", "estan prendidas", "estan apagadas", "esta funcionando", "esta activada",
                                      "estado de", "sigue encendida", "sigue prendida", "quedo encendida", "quedo prendida", NULL};
    for (int i = 0; ask[i]; i++) {
        if (has(sentence, ask[i])) return HOME_STATE;
    }
    static const char *const on[] = {"enciende", "encender", "prende", "prender", "activa", "activar", "enciendeme", "prendeme", "encienda", "prenda", NULL};
    static const char *const off[] = {"apaga", "apagar", "desactiva", "desactivar", "apagame", "apague", NULL};
    for (int i = 0; on[i]; i++) {
        if (has(sentence, on[i])) return HOME_ON;
    }
    for (int i = 0; off[i]; i++) {
        if (has(sentence, off[i])) return HOME_OFF;
    }
    return HOME_NONE;
}

static void trim(const char **a, const char **b) {
    while (*a < *b && isspace((unsigned char)**a)) (*a)++;
    while (*b > *a && isspace((unsigned char)(*b)[-1])) (*b)--;
}

static void copy_range(char *out, size_t size, const char *a, const char *b) {
    trim(&a, &b);
    size_t n = (size_t)(b - a) < size - 1 ? (size_t)(b - a) : size - 1;
    memcpy(out, a, n);
    out[n] = '\0';
}

// The device the sentence names, from the text of the devices file
bool home_find_device(const char *file_text, const char *sentence, home_device_t *device) {
    const char *line = file_text;
    while (*line) {
        const char *end = strchr(line, '\n');
        const char *next = end ? end + 1 : line + strlen(line);
        if (!end) end = line + strlen(line);
        const char *a = line, *b = end;
        trim(&a, &b);
        if (a < b && *a != '#') {
            const char *arrow = NULL;
            for (const char *q = a; q + 1 < b; q++) {
                if (q[0] == '=' && q[1] == '>') { arrow = q; break; }
            }
            if (arrow) {
                bool match = false;
                char first[64] = "";
                const char *p = a;
                while (p <= arrow && !match) {
                    const char *bar = p;
                    while (bar < arrow && *bar != '|') bar++;
                    char raw[96], norm[112];
                    copy_range(raw, sizeof(raw), p, bar);
                    if (!first[0]) snprintf(first, sizeof(first), "%s", raw);
                    local_context_normalize(raw, norm, sizeof(norm));
                    if (strlen(norm) > 3 && strstr(sentence, norm)) match = true;
                    p = bar + 1;
                }
                if (match) {
                    const char *topics = arrow + 2;
                    const char *bar = topics;
                    while (bar < b && *bar != '|') bar++;
                    memset(device, 0, sizeof(*device));
                    snprintf(device->name, sizeof(device->name), "%s", first);
                    copy_range(device->command, sizeof(device->command), topics, bar);
                    if (bar < b) copy_range(device->state, sizeof(device->state), bar + 1, b);
                    return device->command[0] != '\0';
                }
            }
        }
        line = next;
    }
    return false;
}

#ifndef HOME_HOST
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "mqtt_lite.h"
#include "voice_clips.h"
#include "nvs.h"
#include "settings_store.h"
#include <stdlib.h>

static const char *TAG = "HOME";
#define DEVICES_FILE "/sd/home/devices.txt"

bool local_home_broker_configured(void) {
    nvs_handle_t handle;
    size_t n = 0;
    bool have = false;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        have = nvs_get_str(handle, "mqtt", NULL, &n) == ESP_OK && n > 1;
        nvs_close(handle);
    }
    return have;
}

void local_home_set_broker(const char *url) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) != ESP_OK) return;
    if (url && url[0]) nvs_set_str(handle, "mqtt", url);
    else nvs_erase_key(handle, "mqtt");
    nvs_commit(handle);
    nvs_close(handle);
}

// The broker's address: the one saved, or the PC that runs the server
static bool broker_url(char *out, size_t size) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
        size_t n = size;
        bool have = nvs_get_str(handle, "mqtt", out, &n) == ESP_OK && out[0];
        nvs_close(handle);
        if (have) return true;
    }
    bug_settings_t settings;
    settings_copy(&settings);
    const char *host = strstr(settings.server_url, "://");
    host = host ? host + 3 : settings.server_url;
    if (!host[0]) return false;
    snprintf(out, size, "mqtt://%.*s", (int)strcspn(host, ":/"), host);
    return true;
}

// For testing from the USB cable (BUGCFG {"mqtt_test":"topic=ON"} sends; {"mqtt_test":"topic"} reads the retained message)
void local_home_test(const char *spec) {
    char url[160], topic[96], value[64] = "";
    if (!broker_url(url, sizeof(url))) {
        ESP_LOGW(TAG, "No broker address");
        return;
    }
    const char *equals = strchr(spec, '=');
    snprintf(topic, sizeof(topic), "%.*s", (int)(equals ? (size_t)(equals - spec) : strlen(spec)), spec);
    if (equals) {
        ESP_LOGI(TAG, "Test: send %s = %s to %s: %s", topic, equals + 1, url, mqtt_lite_publish(url, topic, equals + 1, false) ? "ok" : "failed");
    } else if (mqtt_lite_get(url, topic, value, sizeof(value), 1800)) {
        ESP_LOGI(TAG, "Test: %s holds \"%s\"", topic, value);
    } else {
        ESP_LOGI(TAG, "Test: nothing read from %s", topic);
    }
}

bool local_home_try(const char *text, local_answer_t *a) {
    home_action_t action = home_parse(text);
    if (action == HOME_NONE) return false;
    FILE *file = fopen(DEVICES_FILE, "rb");
    if (!file) return false;  // no card or no list: not a home command
    char *list = heap_caps_malloc(8 * 1024 + 1, MALLOC_CAP_SPIRAM);  // not from the scarce internal RAM
    if (!list) {
        fclose(file);
        return false;
    }
    size_t n = fread(list, 1, 8 * 1024, file);
    fclose(file);
    list[n] = '\0';
    home_device_t device;
    bool found = home_find_device(list, text, &device);
    free(list);
    if (!found) return false;

    char url[160], reply[160];
    if (!broker_url(url, sizeof(url))) {
        ls_finish(a, "Falta la dirección del servidor de la casa.", NULL);
        ls_speak(a, "Falta la dirección del servidor de la casa.");
        return true;
    }
    if (action == HOME_STATE) {
        char value[32] = "";
        if (!device.state[0]) {
            snprintf(reply, sizeof(reply), "No sé el estado de %s.", device.name);
        } else if (!mqtt_lite_get(url, device.state, value, sizeof(value), 1800)) {
            snprintf(reply, sizeof(reply), "No pude saber el estado de %s.", device.name);
        } else {
            bool on = strcasecmp(value, "ON") == 0 || strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0;
            bool feminine = strncasecmp(device.name, "la ", 3) == 0 || strncasecmp(device.name, "las ", 4) == 0;
            snprintf(reply, sizeof(reply), "%s está %s.", device.name, on ? (feminine ? "encendida" : "encendido") : (feminine ? "apagada" : "apagado"));
            reply[0] = (char)toupper((unsigned char)reply[0]);
        }
        ESP_LOGI(TAG, "%s", reply);
        ls_finish(a, reply, NULL);
        ls_speak(a, reply);
        return true;
    }
    bool ok = mqtt_lite_publish(url, device.command, action == HOME_ON ? "ON" : "OFF", false);
    if (ok) {
        snprintf(reply, sizeof(reply), "Listo, %s %s.", action == HOME_ON ? "encendí" : "apagué", device.name);
        ls_say(a, CLIP_LISTO);
    } else {
        snprintf(reply, sizeof(reply), "No pude hablar con el servidor de la casa.");
    }
    ESP_LOGI(TAG, "%s", reply);
    ls_finish(a, reply, NULL);
    if (!ok) ls_speak(a, reply);  // on success the recorded "Listo" is enough, and works with no internet
    return true;
}
#endif

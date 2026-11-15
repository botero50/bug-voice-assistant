#include "config_backup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "CONFIG_BACKUP";
#define NAMESPACE "bug"
#define FILE_PATH "/sd/bugcfg.js"
#define TEMP_PATH "/sd/bugcfg.tmp"
#define MAX_BLOB 6144

static uint32_t s_last_hash = 0;
static int64_t s_next_check_us = 0;
static volatile bool s_requested = false;

// Things that are not settings and change all the time (the conversation memory)
static bool skip_key(const char *key) {
    return strcmp(key, "hist") == 0 || strcmp(key, "histn") == 0 || strcmp(key, "nobk") == 0;
}

bool config_backup_enabled(void) {
    nvs_handle_t handle;
    uint8_t off = 0;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return true;
    nvs_get_u8(handle, "nobk", &off);
    nvs_close(handle);
    return off == 0;
}

void config_backup_delete(void) {
    remove(FILE_PATH);
    s_last_hash = 0;
}

void config_backup_enable(bool on) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        if (on) nvs_erase_key(handle, "nobk");
        else nvs_set_u8(handle, "nobk", 1);
        nvs_commit(handle);
        nvs_close(handle);
    }
    if (!on) config_backup_delete();
    else config_backup_request();
}

void config_backup_request(void) {
    s_requested = true;
    s_next_check_us = esp_timer_get_time() + 3 * 1000000LL;
}

// Everything in the namespace as JSON text (heap), or NULL
static char *snapshot(void) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "e");
    cJSON_AddNumberToObject(root, "v", 1);
    nvs_iterator_t it = NULL;
    esp_err_t found = nvs_entry_find("nvs", NAMESPACE, NVS_TYPE_ANY, &it);
    while (found == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (!skip_key(info.key)) {
            cJSON *entry = cJSON_CreateObject();
            bool keep = true;
            cJSON_AddStringToObject(entry, "k", info.key);
            switch (info.type) {
            case NVS_TYPE_U8: { uint8_t v; keep = nvs_get_u8(handle, info.key, &v) == ESP_OK; cJSON_AddStringToObject(entry, "t", "u8"); cJSON_AddNumberToObject(entry, "d", v); break; }
            case NVS_TYPE_U16: { uint16_t v; keep = nvs_get_u16(handle, info.key, &v) == ESP_OK; cJSON_AddStringToObject(entry, "t", "u16"); cJSON_AddNumberToObject(entry, "d", v); break; }
            case NVS_TYPE_U32: { uint32_t v; keep = nvs_get_u32(handle, info.key, &v) == ESP_OK; cJSON_AddStringToObject(entry, "t", "u32"); cJSON_AddNumberToObject(entry, "d", v); break; }
            case NVS_TYPE_I32: { int32_t v; keep = nvs_get_i32(handle, info.key, &v) == ESP_OK; cJSON_AddStringToObject(entry, "t", "i32"); cJSON_AddNumberToObject(entry, "d", v); break; }
            case NVS_TYPE_STR: {
                size_t n = 0;
                keep = nvs_get_str(handle, info.key, NULL, &n) == ESP_OK && n > 0 && n < 2048;
                if (keep) {
                    char *text = malloc(n);
                    keep = text && nvs_get_str(handle, info.key, text, &n) == ESP_OK;
                    if (keep) { cJSON_AddStringToObject(entry, "t", "str"); cJSON_AddStringToObject(entry, "d", text); }
                    free(text);
                }
                break;
            }
            case NVS_TYPE_BLOB: {
                size_t n = 0;
                keep = nvs_get_blob(handle, info.key, NULL, &n) == ESP_OK && n > 0 && n <= MAX_BLOB;
                if (keep) {
                    uint8_t *data = malloc(n);
                    size_t encoded_size = 4 * ((n + 2) / 3) + 1, written = 0;
                    unsigned char *encoded = malloc(encoded_size);
                    keep = data && encoded && nvs_get_blob(handle, info.key, data, &n) == ESP_OK &&
                           mbedtls_base64_encode(encoded, encoded_size, &written, data, n) == 0;
                    if (keep) { encoded[written] = '\0'; cJSON_AddStringToObject(entry, "t", "blob"); cJSON_AddStringToObject(entry, "d", (char *)encoded); }
                    free(data);
                    free(encoded);
                }
                break;
            }
            default: keep = false;
            }
            if (keep) cJSON_AddItemToArray(list, entry);
            else cJSON_Delete(entry);
        }
        found = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    nvs_close(handle);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static uint32_t hash_of(const char *text) {
    uint32_t h = 2166136261u;
    for (; *text; text++) h = (h ^ (uint8_t)*text) * 16777619u;
    return h;
}

void config_backup_save(void) {
    struct stat info;
    if (stat("/sd", &info) != 0 || !config_backup_enabled()) return;
    char *text = snapshot();
    if (!text) return;
    uint32_t h = hash_of(text);
    if (h == s_last_hash) {
        free(text);
        return;
    }
    // Nothing to keep yet (a board that was never set up): do not replace a good copy with an empty one
    if (strstr(text, "\"k\":\"ssid\"") == NULL && stat(FILE_PATH, &info) == 0) {
        free(text);
        return;
    }
    FILE *file = fopen(TEMP_PATH, "wb");
    bool ok = false;
    if (file) {
        size_t n = strlen(text);
        ok = fwrite(text, 1, n, file) == n;
        fclose(file);
    }
    if (ok) {
        remove(FILE_PATH);
        ok = rename(TEMP_PATH, FILE_PATH) == 0;
    }
    if (ok) {
        s_last_hash = h;
        ESP_LOGI(TAG, "Settings copied to the card (%u bytes)", (unsigned)strlen(text));
    } else {
        ESP_LOGW(TAG, "Could not write the settings copy");
    }
    free(text);
}

void config_backup_poll(void) {
    int64_t now = esp_timer_get_time();
    if (now < s_next_check_us) return;
    s_next_check_us = now + 60 * 1000000LL;
    s_requested = false;
    config_backup_save();
}

bool config_backup_restore_if_needed(void) {
    nvs_handle_t handle;
    size_t n = 0;
    bool has_wifi = false;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        has_wifi = nvs_get_str(handle, "ssid", NULL, &n) == ESP_OK && n > 1;
        nvs_close(handle);
    }
    if (has_wifi) return false;
    FILE *file = fopen(FILE_PATH, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        fclose(file);
        return false;
    }
    char *text = heap_caps_malloc((size_t)size + 1, MALLOC_CAP_SPIRAM);
    if (!text) {
        fclose(file);
        return false;
    }
    size_t got = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[got] = '\0';
    cJSON *root = cJSON_Parse(text);
    heap_caps_free(text);
    if (!root) return false;
    // A copy without Wi-Fi would restore nothing useful and restart again and again: only a copy that has it is used
    bool copy_has_wifi = false;
    {
        cJSON *probe;
        cJSON_ArrayForEach(probe, cJSON_GetObjectItem(root, "e")) {
            const char *k = cJSON_GetStringValue(cJSON_GetObjectItem(probe, "k"));
            if (k && strcmp(k, "ssid") == 0) copy_has_wifi = true;
        }
    }
    if (!copy_has_wifi) {
        cJSON_Delete(root);
        return false;
    }
    int restored = 0;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, cJSON_GetObjectItem(root, "e")) {
            const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "k"));
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "t"));
            cJSON *data = cJSON_GetObjectItem(entry, "d");
            if (!key || !type || !data || skip_key(key)) continue;
            esp_err_t err = ESP_FAIL;
            if (strcmp(type, "u8") == 0) err = nvs_set_u8(handle, key, (uint8_t)data->valuedouble);
            else if (strcmp(type, "u16") == 0) err = nvs_set_u16(handle, key, (uint16_t)data->valuedouble);
            else if (strcmp(type, "u32") == 0) err = nvs_set_u32(handle, key, (uint32_t)data->valuedouble);
            else if (strcmp(type, "i32") == 0) err = nvs_set_i32(handle, key, (int32_t)data->valuedouble);
            else if (strcmp(type, "str") == 0 && cJSON_IsString(data)) err = nvs_set_str(handle, key, data->valuestring);
            else if (strcmp(type, "blob") == 0 && cJSON_IsString(data)) {
                size_t len = strlen(data->valuestring), out = 0;
                unsigned char *raw = malloc(len);
                if (raw && mbedtls_base64_decode(raw, len, &out, (const unsigned char *)data->valuestring, len) == 0) err = nvs_set_blob(handle, key, raw, out);
                free(raw);
            }
            if (err == ESP_OK) restored++;
        }
        nvs_commit(handle);
        nvs_close(handle);
    }
    cJSON_Delete(root);
    ESP_LOGW(TAG, "The board's memory was empty: %d settings restored from the card", restored);
    return restored > 0;
}

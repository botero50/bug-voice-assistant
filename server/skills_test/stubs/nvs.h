#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
static unsigned char nvs_blob[8192]; static size_t nvs_blob_size = 0;
static inline int nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n; (void)m; *h = 1; return 0; }
static inline int nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *size) { (void)h; (void)k; if (!nvs_blob_size) return 1; memcpy(out, nvs_blob, nvs_blob_size < *size ? nvs_blob_size : *size); *size = nvs_blob_size; return 0; }
static inline int nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t size) { (void)h; (void)k; memcpy(nvs_blob, v, size); nvs_blob_size = size; return 0; }
static inline int nvs_commit(nvs_handle_t h) { (void)h; return 0; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }

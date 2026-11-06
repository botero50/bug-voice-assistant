#ifndef MICROSD_MANAGER_H
#define MICROSD_MANAGER_H

#include "esp_err.h"

typedef struct {
    char model_name[64];
    char version[32];
    uint32_t file_size;
    uint32_t checksum;
    bool is_available;
} ModelInfo;

esp_err_t microsd_init(void);
esp_err_t microsd_deinit(void);
esp_err_t microsd_load_model(const char *model_path, void **buffer, uint32_t *size);
esp_err_t microsd_check_model(const char *model_name, ModelInfo *info);
esp_err_t microsd_save_file(const char *file_path, const void *data, uint32_t size);
esp_err_t microsd_read_file(const char *file_path, void **buffer, uint32_t *size);
uint64_t microsd_get_free_space(void);
uint64_t microsd_get_total_space(void);

#endif

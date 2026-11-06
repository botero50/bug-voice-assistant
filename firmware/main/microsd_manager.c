#include "microsd_manager.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

static const char *TAG = "MICROSD";
static sdmmc_card_t *card = NULL;

// TF slot pins (same map as TamaPoke's pin_config.h)
#define SD_SPI_HOST SPI3_HOST
#define SD_PIN_CLK 2
#define SD_PIN_CMD 1
#define SD_PIN_DATA 3
#define SD_PIN_CS 41

esp_err_t microsd_init(void) {
    ESP_LOGI(TAG, "Initializing SD card...");

    spi_bus_config_t bus_config = {
        .mosi_io_num = SD_PIN_CMD,
        .miso_io_num = SD_PIN_DATA,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };
    esp_err_t bus_ret = spi_bus_initialize(SD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (bus_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SD SPI bus: %s", esp_err_to_name(bus_ret));
        return bus_ret;
    }

    // SPI host configuration
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = 20000;  // 20 MHz for stability

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    // Mount filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sd", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "✓ SD card mounted at /sd");

    return ESP_OK;
}

esp_err_t microsd_deinit(void) {
    if (card) {
        esp_vfs_fat_sdcard_unmount("/sd", card);
        spi_bus_free(SD_SPI_HOST);
        ESP_LOGI(TAG, "SD card unmounted");
        card = NULL;
    }
    return ESP_OK;
}

esp_err_t microsd_load_model(const char *model_path, void **buffer, uint32_t *size) {
    if (!card) {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/sd/models/%s", model_path);

    return microsd_read_file(full_path, buffer, size);
}

esp_err_t microsd_check_model(const char *model_name, ModelInfo *info) {
    if (!card || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    char model_path[256];
    snprintf(model_path, sizeof(model_path), "/sd/models/%s", model_name);

    struct stat st;
    if (stat(model_path, &st) != 0) {
        info->is_available = false;
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(info->model_name, model_name, sizeof(info->model_name) - 1);
    info->file_size = st.st_size;
    info->is_available = true;

    ESP_LOGD(TAG, "Model '%s' found: %u bytes", model_name, info->file_size);
    return ESP_OK;
}

esp_err_t microsd_save_file(const char *file_path, const void *data, uint32_t size) {
    if (!card || !file_path || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(file_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Partial write: %u / %u bytes", written, size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGD(TAG, "File saved: %s (%u bytes)", file_path, size);
    return ESP_OK;
}

esp_err_t microsd_read_file(const char *file_path, void **buffer, uint32_t *size) {
    if (!card || !file_path || !buffer || !size) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(file_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    *size = st.st_size;
    *buffer = malloc(*size);
    if (!*buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file: %u bytes", *size);
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        free(*buffer);
        return ESP_ERR_NOT_FOUND;
    }

    size_t read = fread(*buffer, 1, *size, f);
    fclose(f);

    if (read != *size) {
        free(*buffer);
        ESP_LOGE(TAG, "Partial read: %u / %u bytes", read, *size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGD(TAG, "File loaded: %s (%u bytes)", file_path, *size);
    return ESP_OK;
}

uint64_t microsd_get_free_space(void) {
    if (!card) return 0;
    return (uint64_t)card->csd.capacity * card->csd.sector_size;
}

uint64_t microsd_get_total_space(void) {
    if (!card) return 0;
    return (uint64_t)card->csd.capacity * card->csd.sector_size;
}

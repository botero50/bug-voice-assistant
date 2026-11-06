#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

// Waveshare ESP32-S3-Touch-AMOLED-1.75: 466x466 CO5300 AMOLED over QSPI
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_BITS_PER_PIXEL 16  // RGB565

// Starts the panel and LVGL. The LVGL API may only be used after this returns ESP_OK.
esp_err_t display_init(void);
esp_err_t display_deinit(void);
void display_set_brightness(uint8_t brightness);  // 0-100%
void display_gate_boost_converter(bool enabled);  // Turns the panel on or off to save power
void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
void display_read_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

#endif // DISPLAY_DRIVER_H

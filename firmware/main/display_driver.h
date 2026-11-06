#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"
#include "lvgl.h"

// Display specifications (1.75" AMOLED, 466x466px, capacitive touch)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_BITS_PER_PIXEL 16  // RGB565

// Boost converter control (GPIO 13)
#define DISPLAY_BOOST_CONVERTER_PIN 13

// Touch configuration
#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_I2C_ADDR 0x5A
#define TOUCH_SDA_PIN 41
#define TOUCH_SCL_PIN 40
#define TOUCH_INT_PIN 12

// Display I2C configuration (for AMOLED control)
#define DISPLAY_I2C_PORT I2C_NUM_1
#define DISPLAY_I2C_ADDR 0x3C
#define DISPLAY_SDA_PIN 17
#define DISPLAY_SCL_PIN 18

// Function declarations
esp_err_t display_init(void);
esp_err_t display_deinit(void);
void display_set_brightness(uint8_t brightness);  // 0-100%
void display_gate_boost_converter(bool enabled);  // Power saving: disable when screen off
void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
void display_read_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

#endif // DISPLAY_DRIVER_H

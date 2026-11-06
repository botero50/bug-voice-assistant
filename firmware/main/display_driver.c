#include "display_driver.h"
#include "ui_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "lvgl.h"

static const char *TAG = "DISPLAY_DRIVER";

// Display buffer (allocate in RAM or PSRAM)
static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Touch data cache
static struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} touch_data = {0};

esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Initializing 1.75\" AMOLED display (466x466)");

    // Initialize boost converter pin (GPIO 13) - HIGH = on, LOW = off
    gpio_config_t boost_config = {
        .pin_bit_mask = (1ULL << DISPLAY_BOOST_CONVERTER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boost_config));
    gpio_set_level(DISPLAY_BOOST_CONVERTER_PIN, 1);  // Enable on startup
    ESP_LOGI(TAG, "Boost converter (GPIO 13) enabled");

    // Initialize touch I2C
    ESP_LOGI(TAG, "Initializing touch I2C (0x5A)");
    i2c_config_t touch_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_PIN,
        .scl_io_num = TOUCH_SCL_PIN,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &touch_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    // Initialize touch interrupt pin
    gpio_config_t touch_int_config = {
        .pin_bit_mask = (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_int_config));
    ESP_LOGI(TAG, "Touch interface initialized");

    // Allocate display buffer (full frame)
    // 466x466 * 2 bytes (RGB565) = 434KB per buffer
    // Using two buffers for double buffering
    size_t buf_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t);
    buf1 = (lv_color_t *)malloc(buf_size);
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate display buffer 1 (%u bytes)", buf_size);
        return ESP_ERR_NO_MEM;
    }
    buf2 = (lv_color_t *)malloc(buf_size);
    if (!buf2) {
        ESP_LOGE(TAG, "Failed to allocate display buffer 2 (%u bytes)", buf_size);
        free(buf1);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Display buffers allocated: %.2f MB", (buf_size * 2) / 1e6);

    // Initialize Littlevgl display driver
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.full_refresh = 1;  // Full refresh each frame (AMOLED latency is low)
    lv_disp_drv_register(&disp_drv);
    ESP_LOGI(TAG, "Littlevgl display driver registered");

    // Initialize touch input device
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = display_read_touch_cb;
    lv_indev_drv_register(&indev_drv);
    ESP_LOGI(TAG, "Littlevgl touch driver registered");

    ESP_LOGI(TAG, "Display driver initialization complete");
    return ESP_OK;
}

esp_err_t display_deinit(void) {
    if (buf1) free(buf1);
    if (buf2) free(buf2);
    i2c_driver_delete(TOUCH_I2C_PORT);
    ESP_LOGI(TAG, "Display driver deinitialized");
    return ESP_OK;
}

void display_set_brightness(uint8_t brightness) {
    // brightness: 0-100%
    // Map to I2C command for AMOLED brightness control
    // This would be device-specific; adjust based on actual AMOLED controller
    ESP_LOGD(TAG, "Setting brightness: %d%%", brightness);
    // TODO: Send I2C command to set brightness
}

void display_gate_boost_converter(bool enabled) {
    // CRITICAL POWER SAVING: Gate the boost converter when display is off
    // When enabled=false, saves ~25-35mA continuously
    gpio_set_level(DISPLAY_BOOST_CONVERTER_PIN, enabled ? 1 : 0);
    ESP_LOGD(TAG, "Boost converter: %s", enabled ? "ON" : "OFF (power save)");
}

void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    // This callback is called when Littlevgl needs to flush (update) the display
    // In a real implementation, you would:
    // 1. Write the color data to the display via I2C or SPI
    // 2. Call lv_disp_flush_ready(drv) when done

    // For now, simulate the display update
    // In production, use the actual display controller protocol (e.g., QSPI for AMOLED)

    uint32_t size = lv_area_get_width(area) * lv_area_get_height(area);
    // Simulate write time (AMOLED QSPI transfer)
    // Typical: 466x466 = 217K pixels, at 40MHz QSPI ~ 5-10ms per frame

    lv_disp_flush_ready(drv);
}

void display_read_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    // Read touch input from capacitive touch controller (I2C 0x5A)
    // Return: data->point.x, data->point.y, data->state (pressed/released)

    // Check if touch interrupt is asserted (GPIO 12)
    int touch_int = gpio_get_level(TOUCH_INT_PIN);

    if (touch_int == 0) {
        // Touch detected; read coordinates from I2C
        uint8_t touch_data_buf[5];
        esp_err_t ret = i2c_master_read_from_device(
            TOUCH_I2C_PORT,
            TOUCH_I2C_ADDR,
            touch_data_buf,
            sizeof(touch_data_buf),
            100 / portTICK_PERIOD_MS
        );

        if (ret == ESP_OK) {
            // Parse touch coordinates (format depends on touch controller)
            // Example: bytes 0-1 = X, 2-3 = Y, 4 = pressure
            uint16_t x = (touch_data_buf[0] << 8) | touch_data_buf[1];
            uint16_t y = (touch_data_buf[2] << 8) | touch_data_buf[3];

            // Clamp to display bounds
            if (x > DISPLAY_WIDTH) x = DISPLAY_WIDTH;
            if (y > DISPLAY_HEIGHT) y = DISPLAY_HEIGHT;

            data->point.x = x;
            data->point.y = y;
            data->state = LV_INDEV_STATE_PRESSED;
        }
    } else {
        // No touch
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

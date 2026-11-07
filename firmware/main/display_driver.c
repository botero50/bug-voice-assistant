#include "display_driver.h"
#include "touch_driver.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "DISPLAY_DRIVER";

// Board pins (same map as TamaPoke's pin_config.h)
#define LCD_HOST SPI2_HOST
#define LCD_PIN_D0 4
#define LCD_PIN_D1 5
#define LCD_PIN_D2 6
#define LCD_PIN_D3 7
#define LCD_PIN_SCLK 38
#define LCD_PIN_CS 12
#define LCD_PIN_RESET 39
#define LCD_COL_OFFSET 6

#define I2C_PORT I2C_NUM_0
#define I2C_PIN_SDA 15
#define I2C_PIN_SCL 14

// AXP2101 PMU: BLDO1 powers the panel and can be off after a full battery drain.
#define AXP2101_ADDR 0x34
#define AXP2101_REG_LDO_ONOFF0 0x90
#define AXP2101_REG_BLDO1_VOLTAGE 0x96
#define AXP2101_BLDO1_ENABLE_BIT (1 << 4)
#define AXP2101_BLDO1_3V3 28  // (3300 mV - 500 mV) / 100 mV steps

// CO5300 QSPI framing: each command carries an opcode in its top byte.
#define LCD_OPCODE_CMD 0x02
#define LCD_OPCODE_PIXELS 0x32
#define LCD_CMD(c) (((uint32_t)LCD_OPCODE_CMD << 24) | ((uint32_t)(c) << 8))
#define LCD_RAMWR (((uint32_t)LCD_OPCODE_PIXELS << 24) | ((uint32_t)0x2C << 8))

#define LV_BUFFER_LINES 40
#define LV_TICK_MS 2

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static esp_timer_handle_t tick_timer = NULL;

static esp_err_t pmu_enable_panel_power(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_PORT, &conf), TAG, "I2C config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0), TAG, "I2C install failed");

    const TickType_t timeout = pdMS_TO_TICKS(50);
    uint8_t voltage[] = {AXP2101_REG_BLDO1_VOLTAGE, AXP2101_BLDO1_3V3};
    ESP_RETURN_ON_ERROR(i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, voltage, sizeof(voltage), timeout),
                        TAG, "AXP2101 not responding");

    uint8_t reg = AXP2101_REG_LDO_ONOFF0;
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg, 1, &value, 1, timeout),
                        TAG, "AXP2101 read failed");
    uint8_t enable[] = {AXP2101_REG_LDO_ONOFF0, value | AXP2101_BLDO1_ENABLE_BIT};
    return i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, enable, sizeof(enable), timeout);
}

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx) {
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    return false;
}

static esp_err_t lcd_send(uint8_t cmd, const uint8_t *data, size_t len) {
    return esp_lcd_panel_io_tx_param(lcd_io, LCD_CMD(cmd), data, len);
}

static esp_err_t panel_init(void) {
    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_RESET,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "Reset pin config failed");
    gpio_set_level(LCD_PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus_config = {
        .data0_io_num = LCD_PIN_D0,
        .data1_io_num = LCD_PIN_D1,
        .sclk_io_num = LCD_PIN_SCLK,
        .data2_io_num = LCD_PIN_D2,
        .data3_io_num = LCD_PIN_D3,
        .max_transfer_sz = DISPLAY_WIDTH * LV_BUFFER_LINES * sizeof(lv_color_t) + 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "QSPI bus failed");

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done,
        .user_ctx = &disp_drv,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &lcd_io),
                        TAG, "Panel IO failed");

    ESP_RETURN_ON_ERROR(lcd_send(0x11, NULL, 0), TAG, "Sleep-out failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    const struct {
        uint8_t cmd;
        uint8_t value;
    } init_cmds[] = {
        {0xFE, 0x00},  // Page 0
        {0xC4, 0x80},  // SPI mode
        {0x3A, 0x55},  // RGB565
        {0x53, 0x20},  // Brightness control on
        {0x63, 0xFF},  // High brightness limit
        {0x51, 0xB4},  // Brightness
    };
    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(lcd_send(init_cmds[i].cmd, &init_cmds[i].value, 1), TAG, "Panel init failed");
    }
    return lcd_send(0x29, NULL, 0);  // Display on
}

// The CO5300 only accepts windows that start and end on even boundaries.
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void lv_tick_cb(void *arg) {
    lv_tick_inc(LV_TICK_MS);
}

esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Initializing 1.75\" AMOLED display (466x466)");

    esp_err_t pmu_ret = pmu_enable_panel_power();
    if (pmu_ret != ESP_OK) {
        // The rail is normally already on, so the panel may still work.
        ESP_LOGW(TAG, "Could not set panel power: %s", esp_err_to_name(pmu_ret));
    }

    size_t buf_size = DISPLAY_WIDTH * LV_BUFFER_LINES * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate display buffers (%u bytes each)", buf_size);
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        buf1 = buf2 = NULL;
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISPLAY_WIDTH * LV_BUFFER_LINES);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.rounder_cb = rounder_cb;

    ESP_RETURN_ON_ERROR(panel_init(), TAG, "Panel init failed");

    lv_disp_drv_register(&disp_drv);

    if (touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller not available");
    }
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = display_read_touch_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_args = {.callback = lv_tick_cb, .name = "lv_tick"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "Tick timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LV_TICK_MS * 1000), TAG, "Tick start failed");

    ESP_LOGI(TAG, "Display driver initialization complete");
    return ESP_OK;
}

esp_err_t display_deinit(void) {
    if (tick_timer) {
        esp_timer_stop(tick_timer);
        esp_timer_delete(tick_timer);
        tick_timer = NULL;
    }
    heap_caps_free(buf1);
    heap_caps_free(buf2);
    buf1 = buf2 = NULL;
    i2c_driver_delete(I2C_PORT);
    return ESP_OK;
}

void display_set_brightness(uint8_t brightness) {
    if (!lcd_io) return;
    if (brightness > 100) brightness = 100;
    uint8_t value = (brightness * 255) / 100;
    lcd_send(0x51, &value, 1);
}

void display_gate_boost_converter(bool enabled) {
    if (!lcd_io) return;
    lcd_send(enabled ? 0x29 : 0x28, NULL, 0);
}

void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int x1 = area->x1 + LCD_COL_OFFSET;
    int x2 = area->x2 + LCD_COL_OFFSET;
    uint8_t caset[] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    uint8_t raset[] = {area->y1 >> 8, area->y1 & 0xFF, area->y2 >> 8, area->y2 & 0xFF};
    lcd_send(0x2A, caset, sizeof(caset));
    lcd_send(0x2B, raset, sizeof(raset));

    size_t len = lv_area_get_width(area) * lv_area_get_height(area) * sizeof(lv_color_t);
    esp_err_t ret = esp_lcd_panel_io_tx_color(lcd_io, LCD_RAMWR, color_p, len);
    if (ret != ESP_OK) {
        lv_disp_flush_ready(drv);  // on_color_done won't fire, so don't leave LVGL waiting
    }
}

void display_read_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    bool down = touch_read(&x, &y);
    data->point.x = x;  // LVGL wants the last position on release too
    data->point.y = y;
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

#include "touch_driver.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH";

#define I2C_PORT I2C_NUM_0
#define TOUCH_ADDR 0x5A
#define PIN_RESET 40
#define PIN_INT 11
#define SCREEN_SIZE 466

// Protocol ported from Lewis He's SensorLib (TouchDrvCST92xx), the library TamaPoke uses.
#define REG_READ_HI 0xD0
#define REG_READ_LO 0x00
#define ACK 0xAB
#define EVENT_PRESSED 0x06
#define MAX_FINGERS 2

static volatile bool irq_pending = false;
static bool pressed = false;
static uint16_t last_x = 0;
static uint16_t last_y = 0;

static void IRAM_ATTR touch_isr(void *arg) {
    irq_pending = true;
}

esp_err_t touch_init(void) {
    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << PIN_RESET,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&reset_config);
    gpio_set_level(PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_config_t int_config = {
        .pin_bit_mask = 1ULL << PIN_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&int_config);

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {  // invalid state: already installed
        return ret;
    }
    ESP_LOGI(TAG, "Touch controller reset, waiting for touches");
    return gpio_isr_handler_add(PIN_INT, touch_isr, NULL);
}

bool touch_read(uint16_t *x, uint16_t *y) {
    // Reading while nothing happens can stall the bus, so only read after an interrupt
    // or while a finger is down (to notice when it lifts).
    if (!irq_pending && !pressed) {
        *x = last_x;
        *y = last_y;
        return false;
    }
    irq_pending = false;

    uint8_t command[2] = {REG_READ_HI, REG_READ_LO};
    uint8_t data[MAX_FINGERS * 5 + 5] = {0};
    if (i2c_master_write_read_device(I2C_PORT, TOUCH_ADDR, command, sizeof(command), data, sizeof(data),
                                     pdMS_TO_TICKS(20)) != ESP_OK) {
        pressed = false;
        *x = last_x;
        *y = last_y;
        return false;
    }
    uint8_t ack[3] = {REG_READ_HI, REG_READ_LO, ACK};
    i2c_master_write_to_device(I2C_PORT, TOUCH_ADDR, ack, sizeof(ack), pdMS_TO_TICKS(20));

    uint8_t points = data[5] & 0x7F;
    bool valid = data[0] != ACK && data[6] == ACK && points > 0 && points <= MAX_FINGERS &&
                 (data[0] & 0x0F) == EVENT_PRESSED;
    if (!valid) {
        pressed = false;
        *x = last_x;
        *y = last_y;
        return false;
    }

    uint16_t raw_x = (data[1] << 4) | (data[3] >> 4);
    uint16_t raw_y = (data[2] << 4) | (data[3] & 0x0F);

    // The panel is mounted upside down relative to the touch sensor (TamaPoke mirrors both axes).
    int mapped_x = SCREEN_SIZE - raw_x;
    int mapped_y = SCREEN_SIZE - raw_y;
    last_x = mapped_x < 0 ? 0 : (mapped_x > SCREEN_SIZE - 1 ? SCREEN_SIZE - 1 : mapped_x);
    last_y = mapped_y < 0 ? 0 : (mapped_y > SCREEN_SIZE - 1 ? SCREEN_SIZE - 1 : mapped_y);

    if (!pressed) {
        ESP_LOGI(TAG, "Touch at %u, %u", last_x, last_y);
    }
    pressed = true;
    *x = last_x;
    *y = last_y;
    return true;
}

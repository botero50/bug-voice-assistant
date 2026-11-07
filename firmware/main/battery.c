#include "battery.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>

#define I2C_PORT I2C_NUM_0
#define AXP2101_ADDR 0x34
#define REG_STATUS1 0x00      // bit 5: USB power good, bit 3: battery connected
#define REG_STATUS2 0x01      // bits 7-5: 001 means charging
#define REG_BATTERY_PERCENT 0xA4

static bool read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(50)) == ESP_OK;
}

bool battery_read(battery_status_t *status) {
    uint8_t status1 = 0, status2 = 0, percent = 0;
    if (!read_reg(REG_STATUS1, &status1) || !read_reg(REG_STATUS2, &status2)) {
        return false;
    }

    status->usb_power = status1 & (1 << 5);
    status->charging = (status2 >> 5) == 0x01;
    status->percent = -1;
    if ((status1 & (1 << 3)) && read_reg(REG_BATTERY_PERCENT, &percent)) {
        status->percent = percent > 100 ? 100 : percent;
    }
    return true;
}

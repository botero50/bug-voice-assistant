#include "mem_report.h"
#include "imu.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

static const char *TAG = "IMU";

#define I2C_PORT I2C_NUM_0
#define TIMEOUT pdMS_TO_TICKS(30)
#define REG_WHO_AM_I 0x00
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL7 0x08
#define REG_STATUS0 0x2E
#define REG_AX_L 0x35
#define WHO_AM_I_VALUE 0x05
#define ACCEL_4G_62HZ 0x17  // full scale +-4 g (bits 6..4 = 1), 62.5 Hz (bits 3..0 = 7)
#define LSB_PER_G 8192.0f   // at +-4 g
#define KEEP_SAMPLING_MS 2000

static uint8_t address = 0;
static bool started = false, found = false, enabled = false;
static int settle = 0;  // the first samples after switching on are not real: they are skipped
static volatile uint32_t wanted_until = 0;
static volatile float ax = 0, ay = 0, az = 1;
static volatile bool fresh = false;
static float zero_x = 0, zero_y = 0;
static volatile float shake_energy = 0;
static float last_mag = 1.0f;

static uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT, address, data, 2, TIMEOUT) == ESP_OK;
}

static bool read_regs(uint8_t reg, uint8_t *out, size_t n) {
    return i2c_master_write_read_device(I2C_PORT, address, &reg, 1, out, n, TIMEOUT) == ESP_OK;
}

static void sensor_enable(bool on) {
    if (!found || on == enabled) return;
    write_reg(REG_CTRL7, on ? 0x01 : 0x00);  // bit 0: the accelerometer
    enabled = on;
    settle = 10;
}

static void imu_task(void *arg) {
    mem_task_note("imu", 3072);
    for (;;) {
        bool wanted = (int32_t)(wanted_until - now_ms()) > 0;
        if (!wanted) {
            sensor_enable(false);
            fresh = false;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        sensor_enable(true);
        uint8_t status = 0;
        if (read_regs(REG_STATUS0, &status, 1) && (status & 0x01)) {
            uint8_t raw[6];
            if (read_regs(REG_AX_L, raw, 6)) {
                if (settle > 0) {
                    settle--;
                } else {
                    float x = (int16_t)(raw[0] | (raw[1] << 8)) / LSB_PER_G;
                    float y = (int16_t)(raw[2] | (raw[3] << 8)) / LSB_PER_G;
                    float z = (int16_t)(raw[4] | (raw[5] << 8)) / LSB_PER_G;
                    float mag = sqrtf(x * x + y * y + z * z);
                    float jolt = fabsf(mag - last_mag);
                    last_mag = mag;
                    if (jolt > 0.3f) shake_energy += jolt;  // a shake: the pull changes a lot between samples
                    ax = x;
                    ay = y;
                    az = z;
                    fresh = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

static void start_once(void) {
    if (started) return;
    started = true;
    static const uint8_t ADDRESSES[2] = {0x6B, 0x6A};
    for (int i = 0; i < 2 && !found; i++) {
        address = ADDRESSES[i];
        uint8_t who = 0;
        if (read_regs(REG_WHO_AM_I, &who, 1) && who == WHO_AM_I_VALUE) found = true;
    }
    if (!found) {
        ESP_LOGW(TAG, "The motion sensor was not found");
        return;
    }
    write_reg(REG_CTRL1, 0x40);  // address auto-increment on
    write_reg(REG_CTRL2, ACCEL_4G_62HZ);
    write_reg(REG_CTRL7, 0x00);
    ESP_LOGI(TAG, "Motion sensor found at 0x%02X", address);
    xTaskCreate(imu_task, "imu", 3072, NULL, 2, NULL);
}

bool imu_available(void) {
    start_once();
    return found;
}

void imu_use(void) {
    start_once();
    wanted_until = now_ms() + KEEP_SAMPLING_MS;
}

bool imu_read(float *x, float *y, float *z) {
    imu_use();
    if (!fresh) return false;
    *x = ax;
    *y = ay;
    *z = az;
    return true;
}

void imu_zero(void) {
    imu_use();
    if (!found) return;
    for (int i = 0; i < 30 && !fresh; i++) vTaskDelay(pdMS_TO_TICKS(20));  // the first sample takes a moment
    zero_x = ax;
    zero_y = ay;
}

bool imu_tilt(float *right, float *down) {
    imu_use();
    if (!fresh) return false;
    // On this board X points to the top of the screen and Y to its right (checked with the board on its stand: leaning it
    // forward or back changed X). Tilting the right edge down lowers Y; tilting the bottom edge down raises X.
    *right = -(ay - zero_y);
    *down = ax - zero_x;
    return true;
}

float imu_shake(void) {
    imu_use();
    float e = shake_energy;
    shake_energy = 0;
    return e;
}

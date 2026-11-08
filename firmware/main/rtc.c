#include "rtc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "RTC";

#define I2C_PORT I2C_NUM_0
#define RTC_ADDR 0x51
#define REG_SECONDS 0x04
#define TIMEOUT pdMS_TO_TICKS(50)

static uint8_t to_bcd(int value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int from_bcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0F);
}

// Days since 1970-01-01 for a calendar date (works without any time zone)
static long days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

bool rtc_read(time_t *utc) {
    uint8_t reg = REG_SECONDS;
    uint8_t data[7];
    if (i2c_master_write_read_device(I2C_PORT, RTC_ADDR, &reg, 1, data, sizeof(data), TIMEOUT) != ESP_OK) {
        ESP_LOGW(TAG, "The clock chip does not answer");
        return false;
    }
    if (data[0] & 0x80) {  // oscillator-stop flag: the time cannot be trusted
        ESP_LOGW(TAG, "The clock chip lost its time");
        return false;
    }
    int second = from_bcd(data[0] & 0x7F);
    int minute = from_bcd(data[1] & 0x7F);
    int hour = from_bcd(data[2] & 0x3F);
    int day = from_bcd(data[3] & 0x3F);
    int month = from_bcd(data[5] & 0x1F);
    int year = 2000 + from_bcd(data[6]);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    *utc = (time_t)(days_from_civil(year, month, day) * 86400L + hour * 3600L + minute * 60L + second);
    return true;
}

bool rtc_write(time_t utc) {
    struct tm parts;
    gmtime_r(&utc, &parts);
    uint8_t data[8] = {
        REG_SECONDS,
        to_bcd(parts.tm_sec),  // bit 7 stays 0, which clears the oscillator-stop flag
        to_bcd(parts.tm_min),
        to_bcd(parts.tm_hour),
        to_bcd(parts.tm_mday),
        to_bcd(parts.tm_wday),
        to_bcd(parts.tm_mon + 1),
        to_bcd((parts.tm_year + 1900) % 100),
    };
    esp_err_t err = i2c_master_write_to_device(I2C_PORT, RTC_ADDR, data, sizeof(data), TIMEOUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not set the clock chip: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

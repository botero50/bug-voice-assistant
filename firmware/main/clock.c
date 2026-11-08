#include "clock.h"
#include "rtc.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "CLOCK";

#define CLOCK_VALID_AFTER 1700000000  // before Nov 2023 means it hasn't been set yet

// The internet time arrived: keep the clock chip in step with it
static void synced_cb(struct timeval *tv) {
    if (rtc_write(tv->tv_sec)) {
        ESP_LOGI(TAG, "Internet time received, clock chip updated");
    }
}

void clock_restore(void) {
    setenv("TZ", "<-05>5", 1);  // Colombia: UTC-5, no daylight saving time
    tzset();

    // The clock chip knows the time right away, so alarms work even if the internet is down
    time_t saved = 0;
    bool chip_has_time = rtc_read(&saved) && saved > CLOCK_VALID_AFTER;
    ESP_LOGI(TAG, "Clock chip: %s (%lld)", chip_has_time ? "has the time" : "no valid time", (long long)saved);
    if (time(NULL) < CLOCK_VALID_AFTER && chip_has_time) {
        struct timeval tv = {.tv_sec = saved, .tv_usec = 0};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Time restored from the clock chip");
    }
}

void clock_start(void) {
    clock_restore();
    sntp_set_time_sync_notification_cb(synced_cb);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "Fetching the time from the internet");
}

bool clock_get(int *hour, int *minute) {
    time_t now = time(NULL);
    if (now < CLOCK_VALID_AFTER) {
        return false;
    }
    struct tm local;
    localtime_r(&now, &local);
    *hour = local.tm_hour;
    *minute = local.tm_min;
    return true;
}

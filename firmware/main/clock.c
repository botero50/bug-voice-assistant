#include "clock.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "CLOCK";

void clock_start(void) {
    setenv("TZ", "<-05>5", 1);  // Colombia: UTC-5, no daylight saving time
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "Fetching the time from the internet");
}

bool clock_get(int *hour, int *minute) {
    time_t now = time(NULL);
    if (now < 1700000000) {  // before Nov 2023 means it hasn't been set yet
        return false;
    }
    struct tm local;
    localtime_r(&now, &local);
    *hour = local.tm_hour;
    *minute = local.tm_min;
    return true;
}

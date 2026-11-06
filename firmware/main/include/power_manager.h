#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_err.h"

typedef enum {
    POWER_MODE_ACTIVE,
    POWER_MODE_NORMAL,
    POWER_MODE_SLEEP
} PowerMode;

esp_err_t power_manager_init(void);
esp_err_t power_manager_set_mode(PowerMode mode);
esp_err_t power_manager_set_cpu_freq(uint32_t freq_mhz);
esp_err_t power_manager_set_brightness(uint8_t level);
uint8_t power_manager_get_battery_level(void);
uint32_t power_manager_get_cpu_freq(void);
void power_manager_enable_modem_sleep(bool enable);
void power_manager_idle_timeout_reset(void);

#endif

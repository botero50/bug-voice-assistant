#ifndef POWER_OPTIMIZER_H
#define POWER_OPTIMIZER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Power consumption estimates (milliamps)
#define POWER_CPU_240MHZ 120
#define POWER_CPU_160MHZ 80
#define POWER_CPU_80MHZ 40
#define POWER_WIFI_TX 120
#define POWER_WIFI_IDLE 10
#define POWER_WIFI_OFF 0
#define POWER_AMOLED_FULL_BRIGHT 100
#define POWER_AMOLED_DIMMED 30
#define POWER_AMOLED_OFF 0
#define POWER_BOOST_CONVERTER_ON 30
#define POWER_BOOST_CONVERTER_OFF 0
#define POWER_MICROPHONE_ON 8
#define POWER_MICROPHONE_OFF 0
#define POWER_SYSTEM_IDLE 10

// Battery specifications
#define BATTERY_CAPACITY_MAH 2000
#define BATTERY_VOLTAGE_NOM 3.7  // Nominal voltage

// Power profile enumeration
typedef enum {
    PROFILE_ALWAYS_LISTENING = 0,
    PROFILE_PERIODIC = 1,
    PROFILE_GESTURE_ONLY = 2
} OptimizerProfile;

// Power state structure
typedef struct {
    OptimizerProfile profile;
    uint8_t battery_percent;
    float current_draw_ma;
    float estimated_hours_remaining;
    uint32_t uptime_seconds;
    bool boost_converter_on;
    bool wifi_on;
    bool microphone_on;
    uint8_t display_brightness;
    uint8_t cpu_freq_mhz;
} PowerState;

// Function declarations
esp_err_t power_optimizer_init(void);
void power_optimizer_update_state(void);
PowerState power_optimizer_get_state(void);

// Specific optimizations
void power_optimizer_set_profile(OptimizerProfile profile);
OptimizerProfile power_optimizer_get_profile(void);
void power_optimizer_set_display_brightness(uint8_t brightness);  // 0-100%
void power_optimizer_gate_boost_converter(bool enabled);  // Critical power saving
void power_optimizer_gate_wifi(bool enabled);
void power_optimizer_gate_microphone(bool enabled);
void power_optimizer_set_cpu_frequency(uint8_t mhz);  // 80, 160, 240
float power_optimizer_estimate_battery_hours(void);
void power_optimizer_print_status(void);

#endif // POWER_OPTIMIZER_H

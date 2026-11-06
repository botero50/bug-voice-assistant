#ifndef POWER_PROFILE_MANAGER_H
#define POWER_PROFILE_MANAGER_H

#include <stdint.h>
#include <esp_err.h>

typedef enum {
    POWER_PROFILE_ALWAYS_LISTENING = 0,   // 8mA, <100ms latency
    POWER_PROFILE_PERIODIC_LISTENING = 1, // 2mA, 2s latency (500ms listen, 2s sleep)
    POWER_PROFILE_GESTURE_ONLY = 2,       // 0.5mA, button/tap only
    POWER_PROFILE_MAX
} PowerProfile;

typedef enum {
    POWER_EVENT_ACTIVITY_DETECTED = 0,
    POWER_EVENT_IDLE_TIMEOUT = 1,
    POWER_EVENT_PROFILE_CHANGED = 2,
} PowerProfileEventType;

typedef struct {
    PowerProfileEventType event_type;
    uint32_t timestamp;
} PowerProfileEvent;

esp_err_t power_profile_manager_init(void);
esp_err_t power_profile_manager_set_profile(PowerProfile profile);
PowerProfile power_profile_manager_get_profile(void);

// Activity tracking
void power_profile_manager_notify_activity(void);
void power_profile_manager_notify_idle(uint32_t idle_duration_ms);

// Listen/sleep scheduling
int power_profile_manager_should_listen(void);
int power_profile_manager_is_sleeping(void);
uint32_t power_profile_manager_get_next_wake_time_ms(void);

// Power estimation
float power_profile_manager_get_estimated_power_ma(void);
char *power_profile_manager_get_profile_name(PowerProfile profile);

// Statistics & events
void power_profile_manager_print_stats(void);
int power_profile_manager_get_event(PowerProfileEvent *evt, uint32_t timeout_ms);

#endif

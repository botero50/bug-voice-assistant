#include "power_profile_manager.h"
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static const char *TAG = "POWER_PROFILE";

typedef struct {
    PowerProfile profile;
    uint32_t listen_duration_ms;
    uint32_t sleep_duration_ms;
    uint32_t idle_timeout_ms;
    float estimated_power_ma;
} ProfileConfig;

static const ProfileConfig PROFILES[] = {
    [POWER_PROFILE_ALWAYS_LISTENING] = {
        .profile = POWER_PROFILE_ALWAYS_LISTENING,
        .listen_duration_ms = 0,      // Always on
        .sleep_duration_ms = 0,
        .idle_timeout_ms = UINT32_MAX, // Never switch
        .estimated_power_ma = 8.0,
    },
    [POWER_PROFILE_PERIODIC_LISTENING] = {
        .profile = POWER_PROFILE_PERIODIC_LISTENING,
        .listen_duration_ms = 500,     // Listen 500ms
        .sleep_duration_ms = 2000,     // Sleep 2s
        .idle_timeout_ms = UINT32_MAX, // Always periodic
        .estimated_power_ma = 2.0,
    },
    [POWER_PROFILE_GESTURE_ONLY] = {
        .profile = POWER_PROFILE_GESTURE_ONLY,
        .listen_duration_ms = 0,
        .sleep_duration_ms = 0,
        .idle_timeout_ms = 0,
        .estimated_power_ma = 0.5,
    },
};

typedef struct {
    PowerProfile current_profile;
    PowerProfile previous_profile;
    uint32_t profile_start_time;
    uint32_t last_activity_time;
    volatile int listening;
    volatile int sleeping;
    uint32_t total_listen_cycles;
    uint32_t total_sleep_cycles;
} PowerProfileState;

static PowerProfileState profile_state = {
    .current_profile = POWER_PROFILE_ALWAYS_LISTENING,
    .previous_profile = POWER_PROFILE_ALWAYS_LISTENING,
    .listening = 0,
    .sleeping = 0,
    .total_listen_cycles = 0,
    .total_sleep_cycles = 0,
};

static QueueHandle_t profile_event_queue = NULL;

esp_err_t power_profile_manager_init(void) {
    profile_event_queue = xQueueCreate(10, sizeof(PowerProfileEvent));
    if (!profile_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    profile_state.profile_start_time = xTaskGetTickCount();
    profile_state.last_activity_time = profile_state.profile_start_time;

    ESP_LOGI(TAG, "Power profile manager initialized");
    ESP_LOGI(TAG, "Initial profile: ALWAYS_LISTENING (8mA)");

    return ESP_OK;
}

esp_err_t power_profile_manager_set_profile(PowerProfile profile) {
    if (profile >= POWER_PROFILE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    profile_state.previous_profile = profile_state.current_profile;
    profile_state.current_profile = profile;
    profile_state.profile_start_time = xTaskGetTickCount();

    const ProfileConfig *cfg = &PROFILES[profile];
    ESP_LOGI(TAG, "Profile changed: %d → %d", profile_state.previous_profile, profile);
    ESP_LOGI(TAG, "  Listen: %dms, Sleep: %dms, Est. Power: %.1fmA",
             cfg->listen_duration_ms, cfg->sleep_duration_ms, cfg->estimated_power_ma);

    return ESP_OK;
}

PowerProfile power_profile_manager_get_profile(void) {
    return profile_state.current_profile;
}

void power_profile_manager_notify_activity(void) {
    profile_state.last_activity_time = xTaskGetTickCount();

    // Switch to always-listening for 30 seconds after activity
    if (profile_state.current_profile == POWER_PROFILE_PERIODIC_LISTENING) {
        power_profile_manager_set_profile(POWER_PROFILE_ALWAYS_LISTENING);

        PowerProfileEvent evt = {
            .event_type = POWER_EVENT_ACTIVITY_DETECTED,
            .timestamp = profile_state.last_activity_time,
        };
        xQueueSend(profile_event_queue, &evt, pdMS_TO_TICKS(100));
    }
}

void power_profile_manager_notify_idle(uint32_t idle_duration_ms) {
    // Switch to periodic listening after 2 minutes idle
    if (idle_duration_ms > 120000 &&
        profile_state.current_profile == POWER_PROFILE_ALWAYS_LISTENING) {
        power_profile_manager_set_profile(POWER_PROFILE_PERIODIC_LISTENING);

        PowerProfileEvent evt = {
            .event_type = POWER_EVENT_IDLE_TIMEOUT,
            .timestamp = xTaskGetTickCount(),
        };
        xQueueSend(profile_event_queue, &evt, pdMS_TO_TICKS(100));
    }
}

int power_profile_manager_should_listen(void) {
    const ProfileConfig *cfg = &PROFILES[profile_state.current_profile];

    if (cfg->listen_duration_ms == 0) {
        // Always listening or gesture-only mode
        return 1;
    }

    // Periodic mode: calculate if we should be listening or sleeping
    uint32_t elapsed = xTaskGetTickCount() - profile_state.profile_start_time;
    uint32_t cycle_period = cfg->listen_duration_ms + cfg->sleep_duration_ms;
    uint32_t position_in_cycle = elapsed % cycle_period;

    return position_in_cycle < cfg->listen_duration_ms;
}

int power_profile_manager_is_sleeping(void) {
    return !power_profile_manager_should_listen() &&
           PROFILES[profile_state.current_profile].listen_duration_ms > 0;
}

uint32_t power_profile_manager_get_next_wake_time_ms(void) {
    const ProfileConfig *cfg = &PROFILES[profile_state.current_profile];

    if (cfg->listen_duration_ms == 0) {
        return UINT32_MAX; // No sleep
    }

    uint32_t elapsed = xTaskGetTickCount() - profile_state.profile_start_time;
    uint32_t cycle_period = cfg->listen_duration_ms + cfg->sleep_duration_ms;
    uint32_t position_in_cycle = elapsed % cycle_period;

    if (position_in_cycle < cfg->listen_duration_ms) {
        // Currently listening, next wake is after sleep
        return cfg->sleep_duration_ms - (position_in_cycle - cfg->listen_duration_ms);
    } else {
        // Currently sleeping
        return cfg->listen_duration_ms + cfg->sleep_duration_ms - position_in_cycle;
    }
}

float power_profile_manager_get_estimated_power_ma(void) {
    const ProfileConfig *cfg = &PROFILES[profile_state.current_profile];
    return cfg->estimated_power_ma;
}

char *power_profile_manager_get_profile_name(PowerProfile profile) {
    switch (profile) {
        case POWER_PROFILE_ALWAYS_LISTENING:
            return "ALWAYS_LISTENING (8mA, <100ms latency)";
        case POWER_PROFILE_PERIODIC_LISTENING:
            return "PERIODIC (2mA, 2s latency)";
        case POWER_PROFILE_GESTURE_ONLY:
            return "GESTURE_ONLY (0.5mA, button/tap only)";
        default:
            return "UNKNOWN";
    }
}

void power_profile_manager_print_stats(void) {
    ESP_LOGI(TAG, "\n╔═══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Power Profile Statistics                 ║");
    ESP_LOGI(TAG, "╠═══════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Current: %s", power_profile_manager_get_profile_name(profile_state.current_profile));
    ESP_LOGI(TAG, "║ Est. Power: %.1fmA", power_profile_manager_get_estimated_power_ma());
    ESP_LOGI(TAG, "║ Listen cycles: %u", profile_state.total_listen_cycles);
    ESP_LOGI(TAG, "║ Sleep cycles: %u", profile_state.total_sleep_cycles);

    uint32_t uptime = xTaskGetTickCount();
    uint32_t hours = uptime / (3600000 / portTICK_PERIOD_MS);
    uint32_t mins = (uptime % (3600000 / portTICK_PERIOD_MS)) / (60000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "║ Uptime: %u hours %u mins", hours, mins);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════╝\n");
}

int power_profile_manager_get_event(PowerProfileEvent *evt, uint32_t timeout_ms) {
    if (!evt) return 0;

    if (xQueueReceive(profile_event_queue, evt, pdMS_TO_TICKS(timeout_ms))) {
        return 1;
    }
    return 0;
}

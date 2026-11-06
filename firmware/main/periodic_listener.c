#include "periodic_listener.h"
#include "power_profile_manager.h"
#include "audio_driver.h"
#include "power_manager.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "PERIODIC_LISTENER";

typedef struct {
    volatile int enabled;
    volatile int listening;
    uint32_t listen_start_time;
    uint32_t cycles_completed;
    TaskHandle_t listener_task_handle;
} PeriodicListenerState;

static PeriodicListenerState listener_state = {
    .enabled = 0,
    .listening = 0,
    .cycles_completed = 0,
};

static void periodic_listener_task(void *pvParameters) {
    ESP_LOGI(TAG, "Periodic listener task started");

    while (listener_state.enabled) {
        PowerProfile profile = power_profile_manager_get_profile();

        if (profile == POWER_PROFILE_ALWAYS_LISTENING) {
            // Always listening mode - just wait
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (profile == POWER_PROFILE_GESTURE_ONLY) {
            // Gesture only - don't listen, wait for button press
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Periodic listening mode
        if (power_profile_manager_should_listen()) {
            if (!listener_state.listening) {
                // Wake up - enable audio
                listener_state.listening = 1;
                listener_state.listen_start_time = xTaskGetTickCount();

                audio_enable_input();
                power_manager_set_mode(POWER_MODE_ACTIVE);

                ESP_LOGD(TAG, "Periodic listening: ON (cycle %u)", listener_state.cycles_completed);
            }
        } else {
            if (listener_state.listening) {
                // Sleep - disable audio
                listener_state.listening = 0;
                listener_state.cycles_completed++;

                audio_disable_input();
                power_manager_set_mode(POWER_MODE_MODEM_SLEEP);

                ESP_LOGD(TAG, "Periodic listening: OFF (sleeping, cycle %u)", listener_state.cycles_completed);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250)); // Check every 250ms
    }

    ESP_LOGI(TAG, "Periodic listener task stopped");
    vTaskDelete(NULL);
}

esp_err_t periodic_listener_init(void) {
    if (listener_state.enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    listener_state.enabled = 1;
    listener_state.listening = 0;
    listener_state.cycles_completed = 0;

    BaseType_t ret = xTaskCreatePinnedToCore(
        periodic_listener_task,
        "periodic_listener",
        4096,
        NULL,
        5,
        &listener_state.listener_task_handle,
        0);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create periodic listener task");
        listener_state.enabled = 0;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Periodic listener initialized");
    return ESP_OK;
}

void periodic_listener_deinit(void) {
    listener_state.enabled = 0;
    if (listener_state.listener_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int periodic_listener_is_listening(void) {
    return listener_state.listening;
}

uint32_t periodic_listener_get_cycles(void) {
    return listener_state.cycles_completed;
}

void periodic_listener_print_stats(void) {
    ESP_LOGI(TAG, "\n╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Periodic Listener Statistics              ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Currently: %s", listener_state.listening ? "LISTENING" : "SLEEPING");
    ESP_LOGI(TAG, "║ Cycles: %u", listener_state.cycles_completed);
    ESP_LOGI(TAG, "║ Mode: %s", power_profile_manager_get_profile_name(
        power_profile_manager_get_profile()));
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝\n");
}

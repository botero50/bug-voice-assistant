#include "microphone_duty_cycle.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MIC_DUTY";

static MicrophoneDutyCycle mic_state = {
    .listening = true,
    .cycle_start_time_ms = 0,
    .listen_duration_ms = MIC_DUTY_LISTEN_MS,
    .sleep_duration_ms = MIC_DUTY_SLEEP_MS,
    .total_cycles = 0
};

static uint32_t init_time_ms = 0;
static uint32_t forced_listen_until_ms = 0;

void mic_duty_cycle_init(uint32_t listen_ms, uint32_t sleep_ms) {
    ESP_LOGI(TAG, "Initializing microphone duty cycle");
    ESP_LOGI(TAG, "Listen: %d ms, Sleep: %d ms", listen_ms, sleep_ms);

    mic_state.listen_duration_ms = listen_ms;
    mic_state.sleep_duration_ms = sleep_ms;
    mic_state.listening = true;
    mic_state.cycle_start_time_ms = 0;

    init_time_ms = esp_timer_get_time() / 1000;
}

void mic_duty_cycle_update(void) {
    uint32_t current_time_ms = (esp_timer_get_time() / 1000) - init_time_ms;

    // Check if forced listen is active
    if (forced_listen_until_ms > current_time_ms) {
        mic_state.listening = true;
        return;
    }

    // Calculate position in current cycle
    uint32_t cycle_duration = mic_state.listen_duration_ms + mic_state.sleep_duration_ms;
    uint32_t position_in_cycle = current_time_ms % cycle_duration;

    // Determine if in listen or sleep phase
    bool should_listen = (position_in_cycle < mic_state.listen_duration_ms);

    // Check for cycle transition
    if (should_listen != mic_state.listening) {
        mic_state.listening = should_listen;

        if (should_listen) {
            mic_state.cycle_start_time_ms = current_time_ms;
            mic_state.total_cycles++;
            ESP_LOGI(TAG, "Microphone LISTEN started (cycle %d)", mic_state.total_cycles);
        } else {
            ESP_LOGI(TAG, "Microphone SLEEP started (%d ms)", mic_state.sleep_duration_ms);
        }
    }
}

bool mic_duty_cycle_should_listen(void) {
    mic_duty_cycle_update();
    return mic_state.listening;
}

MicrophoneDutyCycle mic_duty_cycle_get_state(void) {
    mic_duty_cycle_update();
    return mic_state;
}

void mic_duty_cycle_force_listen(void) {
    // Force listening for the full listen duration
    uint32_t current_time_ms = (esp_timer_get_time() / 1000) - init_time_ms;
    forced_listen_until_ms = current_time_ms + mic_state.listen_duration_ms;

    ESP_LOGI(TAG, "Forcing microphone to listen for %d ms", mic_state.listen_duration_ms);
}

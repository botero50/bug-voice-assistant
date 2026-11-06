#include "voice_activity_detection.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "VAD";

static VADState vad_state = {
    .energy_level = 0.0,
    .voice_detected = false,
    .silence_duration_ms = 0,
    .last_voice_time_ms = 0
};

static float energy_threshold = VAD_ENERGY_THRESHOLD;
static uint32_t init_time_ms = 0;

void vad_init(void) {
    ESP_LOGI(TAG, "Initializing Voice Activity Detection");
    init_time_ms = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "VAD threshold: %.1f (energy units)", energy_threshold);
}

static float calculate_frame_energy(const int16_t *audio_data, uint32_t sample_count) {
    // Calculate RMS (root mean square) energy of the frame
    double sum = 0.0;

    for (uint32_t i = 0; i < sample_count; i++) {
        double sample = audio_data[i] / 32768.0;  // Normalize to [-1.0, 1.0]
        sum += sample * sample;
    }

    double rms = sqrt(sum / sample_count);
    // Convert to dB scale (0-100)
    float energy_db = 20.0 * log10(rms + 1e-10) + 90.0;  // Offset to 0-100 range

    return energy_db > 0 ? energy_db : 0;
}

void vad_update(const int16_t *audio_data, uint32_t sample_count) {
    if (!audio_data || sample_count == 0) {
        return;
    }

    // Calculate energy of current frame
    float current_energy = calculate_frame_energy(audio_data, sample_count);
    vad_state.energy_level = current_energy;

    // Get current time
    uint32_t current_time_ms = (esp_timer_get_time() / 1000) - init_time_ms;

    // Detect voice based on energy threshold
    bool current_voice = (current_energy > energy_threshold);

    if (current_voice) {
        // Voice detected
        vad_state.voice_detected = true;
        vad_state.last_voice_time_ms = current_time_ms;
        vad_state.silence_duration_ms = 0;
        ESP_LOGD(TAG, "Voice detected: energy=%.1f", current_energy);
    } else {
        // No voice
        vad_state.silence_duration_ms = current_time_ms - vad_state.last_voice_time_ms;

        // Check if silence timeout exceeded
        if (vad_state.silence_duration_ms > VAD_SILENCE_TIMEOUT_MS) {
            vad_state.voice_detected = false;
            ESP_LOGD(TAG, "Silence timeout: no voice for %d ms", vad_state.silence_duration_ms);
        }
    }
}

bool vad_is_voice_active(void) {
    return vad_state.voice_detected;
}

float vad_get_energy_level(void) {
    return vad_state.energy_level;
}

VADState vad_get_state(void) {
    return vad_state;
}

void vad_set_threshold(float threshold) {
    energy_threshold = threshold;
    ESP_LOGI(TAG, "VAD threshold updated: %.1f", threshold);
}

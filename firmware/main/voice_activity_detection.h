#ifndef VOICE_ACTIVITY_DETECTION_H
#define VOICE_ACTIVITY_DETECTION_H

#include <stdint.h>
#include <stdbool.h>

// VAD configuration
#define VAD_FRAME_SIZE 512  // Samples per frame (32ms at 16kHz)
#define VAD_ENERGY_THRESHOLD 100  // Threshold (energy units)
#define VAD_SILENCE_TIMEOUT_MS 2000  // Timeout after no voice detected

// VAD state
typedef struct {
    float energy_level;
    bool voice_detected;
    uint32_t silence_duration_ms;
    uint32_t last_voice_time_ms;
} VADState;

// Function declarations
void vad_init(void);
void vad_update(const int16_t *audio_data, uint32_t sample_count);
bool vad_is_voice_active(void);
float vad_get_energy_level(void);
VADState vad_get_state(void);
void vad_set_threshold(float threshold);

#endif // VOICE_ACTIVITY_DETECTION_H

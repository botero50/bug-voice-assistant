#ifndef WAKE_WORD_DETECTOR_H
#define WAKE_WORD_DETECTOR_H

#include "esp_err.h"

typedef enum {
    WAKE_WORD_LISTENING,
    WAKE_WORD_DETECTED,
    PROCESSING,
    IDLE
} WakeWordState;

typedef struct {
    WakeWordState state;
    float confidence;
    uint32_t last_detection_time;
    uint32_t false_positive_count;
    uint32_t true_positive_count;
} WakeWordContext;

esp_err_t wake_word_detector_init(void);
esp_err_t wake_word_detector_process(const int16_t *audio_frame, int frame_size);
WakeWordState wake_word_detector_get_state(void);
float wake_word_detector_get_confidence(void);
void wake_word_detector_reset(void);

#endif

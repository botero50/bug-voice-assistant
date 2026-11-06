#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include "esp_err.h"
#include "driver/i2s_std.h"

typedef enum {
    AUDIO_DIR_IN,
    AUDIO_DIR_OUT,
    AUDIO_DIR_DUPLEX
} AudioDirection;

esp_err_t audio_init(AudioDirection direction);
esp_err_t audio_start_recording(void);
esp_err_t audio_stop_recording(void);
esp_err_t audio_read_samples(int16_t *buffer, size_t buffer_size, size_t *read_size);
esp_err_t audio_write_samples(const int16_t *buffer, size_t buffer_size);
esp_err_t audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);
esp_err_t audio_deinit(void);

#endif

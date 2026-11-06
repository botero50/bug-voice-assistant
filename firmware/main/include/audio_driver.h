#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_DIR_IN,
    AUDIO_DIR_OUT,
    AUDIO_DIR_DUPLEX
} AudioDirection;

// Requires the board's I2C bus, which display_init() starts.
esp_err_t audio_init(AudioDirection direction);
esp_err_t audio_start_recording(void);
esp_err_t audio_stop_recording(void);

// Samples are 16 kHz, 16-bit mono. Sizes are in bytes.
esp_err_t audio_read_samples(int16_t *buffer, size_t buffer_size, size_t *read_size);
esp_err_t audio_write_samples(const int16_t *buffer, size_t buffer_size);

// Turns the speaker amplifier on or off. Keep it off when idle to avoid hiss.
void audio_set_speaker(bool enabled);
esp_err_t audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);
esp_err_t audio_deinit(void);

#endif

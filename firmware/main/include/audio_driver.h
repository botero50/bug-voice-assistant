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

// Music: 32 kHz, 16-bit mono (twice the bandwidth of the voice path). Sizes are in bytes.
#define AUDIO_MUSIC_RATE 32000
esp_err_t audio_write_music(const int16_t *buffer, size_t buffer_size);
float audio_output_level(void);  // 0..1, how loud the sound being played is

// Turns the speaker amplifier on or off. Keep it off when idle to avoid hiss.
void audio_set_speaker(bool enabled);

// Plays a short sine tone. The speaker amplifier must already be on.
esp_err_t audio_beep(float frequency_hz, int milliseconds);

// Throws away microphone audio for a while, e.g. right after the speaker played.
void audio_flush_input(int milliseconds);
// Copies the raw stream (both microphones, 32 kHz, interleaved left/right) into `buffer` until audio_raw_tap_stop(), which returns the int16 count.
void audio_raw_tap_start(int16_t *buffer, size_t capacity_samples);
size_t audio_raw_tap_stop(void);
esp_err_t audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);
esp_err_t audio_deinit(void);

#endif

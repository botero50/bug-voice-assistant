#ifndef AUDIO_CIRCULAR_BUFFER_H
#define AUDIO_CIRCULAR_BUFFER_H

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 16000
#define CIRCULAR_BUFFER_SIZE (AUDIO_SAMPLE_RATE * 5)  // 5 seconds @ 16kHz

void audio_circular_buffer_init(void);
void audio_circular_buffer_write(const int16_t *samples, int count);
int audio_circular_buffer_read(int16_t *out, int count);
int audio_circular_buffer_available(void);
void audio_circular_buffer_flush(void);
float audio_circular_buffer_fill_percent(void);

#endif

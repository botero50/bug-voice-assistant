#ifndef VOICE_LOOP_H
#define VOICE_LOOP_H

#include "driver/i2s_std.h"

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNELS 1
#define AUDIO_BUFFER_SIZE (AUDIO_SAMPLE_RATE * 2)  // 2 seconds max

typedef struct {
    int16_t *buffer;
    int buffer_size;
    int write_pos;
    int valid_samples;
} AudioBuffer;

void voice_loop_init(void);
void voice_loop_start_capture(void);
void voice_loop_stop_capture(void);
int voice_loop_get_audio_data(int16_t **buffer, int *buffer_size);
void voice_loop_process(void);

#endif

#ifndef AUDIO_SELFTEST_H
#define AUDIO_SELFTEST_H

// Repeats a speaker tone, a mic recording and its playback so audio can be
// checked on the board. Temporary until the real voice path exists.
void audio_selftest_start(void);

// Current step, for the main loop to show on screen. Returns NULL if unchanged.
const char *audio_selftest_take_status(void);

#endif

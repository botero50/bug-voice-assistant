#ifndef WAKE_LOCAL_H
#define WAKE_LOCAL_H

#include <stdbool.h>
#include <stdint.h>

// The "Hola Bug" detector that runs on the board. It listens to the microphone frames (20 ms, 16 kHz) and,
// every 100 ms, looks at the last 1.5 seconds with a small neural network trained on the PC
// (server/wake/train.py). When it is sure it heard the greeting, wake_local_feed() returns true, so the
// green ring can appear at once instead of after the PC has recognised the speech.

// Feed one 320-sample frame. Returns true when the wake word was just detected.
bool wake_local_feed(const int16_t *frame);

// The score of the look just made (0 to 1), or -1 if the last frame did not make a look. For testing.
float wake_local_last_score(void);

// Forget what was heard (after the board spoke, or the microphone was off), so old audio cannot trigger it.
void wake_local_reset(void);

#endif

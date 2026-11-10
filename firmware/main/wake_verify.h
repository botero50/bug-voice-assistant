#ifndef WAKE_VERIFY_H
#define WAKE_VERIFY_H

#include <stddef.h>
#include <stdint.h>

// The board's own second check of "Hola Bug" (trained on the PC by server/wake/train_verifier.py from the detector's real mistakes).
// It looks at a recorded utterance and says how sure it is that it was the greeting, 0 to 1. No PC and no internet needed.

// The probability that the utterance (16 kHz, as recorded after the board heard a sound) is "Hola Bug". The window starts a little before the voice does.
float wake_verify_score(const int16_t *pcm, size_t samples);

// The probability from which the board accepts the greeting on its own (default 0.70; 0 turns this check off). Kept in the board's memory.
float wake_verify_threshold(void);
void wake_verify_set_threshold(float value);

#endif

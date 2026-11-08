#ifndef CLIPS_PLAYER_H
#define CLIPS_PLAYER_H

#include <stdbool.h>

// The board's own voice: words and short phrases recorded once (server/clips/make_clips.py) and kept in the
// "storage" partition. Answers the board gives by itself are made by joining them, so no PC is involved.

// Reads the index. False when the partition holds no clips (then the board falls back to the PC's voice).
bool clips_init(void);

// Plays the clips one after another through the speaker (blocking). ids are values of clip_id_t.
void clips_play(const int *ids, int count);

#endif

#ifndef VOICE_ID_H
#define VOICE_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "voice_id_model.h"

// "Who is speaking": a small, honest voice comparison that runs on the board. It is good enough to greet people by name,
// NOT to protect anything: never decide a permission or a private answer with it.
//
// An utterance becomes VID_FEATURES numbers (cepstral mean and spread over the speech frames, plus pitch), then VID_DIMS numbers
// through a fixed projection learned offline (server/wake/train_speaker.py). A person is the average of a few such vectors.

#define VID_MAX_PEOPLE 4
#define VID_NAME_MAX 24

// The 28 raw features of 16 kHz mono audio (the same numbers server/wake/speaker_embed.py makes); false if there is too little speech
bool voice_id_features(const int16_t *audio, size_t samples, float *features);
// The projected vector (VID_DIMS numbers) of an utterance; false if there is too little speech
bool voice_id_embed(const int16_t *audio, size_t samples, float *embedding);

// People known to the board (kept in flash). Names are stored as given, compared without capitals.
void voice_id_init(void);
int voice_id_count(void);
const char *voice_id_name(int index);
// Saves a person from several utterances' vectors (replaces a person with the same name); false if the list is full or the input is bad
bool voice_id_enroll(const char *name, const float *embeddings, int count);
bool voice_id_forget(const char *name);  // NULL or "" forgets everybody
// The person who sounds most like this vector: their index and the similarity (-1..1); -1 if nobody is known or none reaches the threshold
int voice_id_identify(const float *embedding, float *score);
float voice_id_similarity(const float *a, const float *b);

#endif

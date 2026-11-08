#ifndef COMMAND_LOCAL_H
#define COMMAND_LOCAL_H

#include <stddef.h>
#include <stdint.h>

// The command recogniser that runs on the board (trained on the PC from the owner's voice, server/wake/train_commands.py).
// It listens to the sentence recorded after "Hola Bug" and decides which of the fixed commands it was, or that it
// was none of them. No PC and no internet needed.

// What the recogniser thinks the sentence is: a command id, "wake" for the greeting ("Hola Bug") or "unknown".
// `confidence` is 0 to 1; nothing is decided here.
const char *command_local_classify(const int16_t *pcm, size_t samples, float *confidence);

// Returns the sentence the board's skills understand for the recognised command ("que hora es", "temporizador de
// cinco minutos"...), or NULL when it is not one of them or the board is not sure. `confidence` is 0 to 1.
const char *command_local_recognize(const int16_t *pcm, size_t samples, float *confidence);

#endif

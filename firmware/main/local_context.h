#ifndef LOCAL_CONTEXT_H
#define LOCAL_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>

// "Repite lo último", "qué dijiste": the owner wants the last answer again
bool local_context_is_repeat(const char *original);

// A short follow-up ("y mañana", "y en Bogotá", "y el euro", "y Shakira") rewritten as a whole question using what was asked before; false if it is not one
bool local_context_expand(const char *original, const char *last_heard, char *out, size_t size);

#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
static inline size_t strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; } return l; }
static inline size_t strlcat(char *d, const char *s, size_t n) { size_t dl = strlen(d); if (dl >= n) return dl + strlen(s); return dl + strlcpy(d + dl, s, n - dl); }
#define MALLOC_CAP_SPIRAM 0
static inline void *heap_caps_malloc(size_t n, int c) { (void)c; return malloc(n); }
#include <time.h>
static inline struct tm *localtime_r(const time_t *t, struct tm *out) { struct tm *p = localtime(t); if (!p) return NULL; *out = *p; return out; }

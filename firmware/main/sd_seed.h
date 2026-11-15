#ifndef SD_SEED_H
#define SD_SEED_H

#include <stdbool.h>

// Copies the example folders (skills, home) to a freshly used microSD card; does nothing on a card it already did
// force = true puts the examples back even on a card that has them (BUGCFG {"sd_restore":1})
void sd_seed_defaults(bool force);

#endif

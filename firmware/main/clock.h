#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

// Starts internet time (Colombia, UTC-5). Call once Wi-Fi is connected.
void clock_start(void);

// False until the time has been fetched.
bool clock_get(int *hour, int *minute);

#endif

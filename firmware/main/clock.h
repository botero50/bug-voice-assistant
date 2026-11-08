#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

// Sets the time from the board's clock chip, if it has one saved. Call early (after the display
// started the I2C bus), so alarms work even when the internet is down.
void clock_restore(void);

// Starts internet time (Colombia, UTC-5). Call once Wi-Fi is connected.
void clock_start(void);

// False until the time has been fetched.
bool clock_get(int *hour, int *minute);

#endif

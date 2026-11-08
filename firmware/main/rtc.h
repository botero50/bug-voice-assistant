#ifndef RTC_H
#define RTC_H

#include <stdbool.h>
#include <time.h>

// The board's PCF85063 clock chip (I2C 0x51) keeps the time while the ESP32 restarts, so timers
// and alarms know the time even before the internet answers. Times are UTC.

// Reads the chip. False if it is missing, or its time was never set (or was lost).
bool rtc_read(time_t *utc);

// Stores the time in the chip.
bool rtc_write(time_t utc);

#endif

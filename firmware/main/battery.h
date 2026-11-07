#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>

typedef struct {
    int percent;     // 0-100, or -1 if no battery is connected
    bool charging;
    bool usb_power;
} battery_status_t;

// Reads the AXP2101 power chip. Needs the board's I2C bus, which display_init() starts.
bool battery_read(battery_status_t *status);

#endif

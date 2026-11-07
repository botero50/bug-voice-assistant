#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// CST9217 capacitive touch on the board's I2C bus (the bus display_init() starts).
esp_err_t touch_init(void);

// Returns true while a finger is down, with its position in screen pixels (0-465).
bool touch_read(uint16_t *x, uint16_t *y);

#endif

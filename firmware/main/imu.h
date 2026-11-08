#ifndef IMU_H
#define IMU_H

#include <stdbool.h>

// The board's motion sensor (QMI8658 accelerometer, on the same I2C bus as the touch panel). It is only read while a game asks
// for it (imu_use), so it costs nothing the rest of the time. The sensor chip, register values and the way the tilt axes map to
// the screen follow the TamaPoke project (MIT, see CREDITS.md).

bool imu_available(void);                       // the sensor was found (checked on first use)
void imu_use(void);                             // call often while tilting is wanted: it samples for the next 2 seconds
void imu_zero(void);                            // the pose the board is in now becomes "level"
bool imu_tilt(float *right, float *down);       // tilt since imu_zero in g: right > 0 when the right side goes down, down > 0 when the bottom does
float imu_shake(void);                          // how much the board was shaken since the last call (0 at rest, about 1 for a good shake)
bool imu_read(float *x, float *y, float *z);    // the last raw sample, in g (for tests)

#endif

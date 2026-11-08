#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <stdbool.h>
#include <stdint.h>

// The stopwatch lives here, not in its screen, so it keeps counting while another screen is open.

// "start" (also resumes), "pause" or "reset".
void stopwatch_command(const char *command);

void stopwatch_toggle(void);  // start if stopped, pause if running
bool stopwatch_running(void);

// Time counted so far, in milliseconds.
int64_t stopwatch_elapsed_ms(void);

#endif

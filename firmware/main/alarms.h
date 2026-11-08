#ifndef ALARMS_H
#define ALARMS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Timers, alarms and reminders. The server works out when ("despiértame a las 6 y media") and
// hands the board a time; the board keeps it in its own memory and rings when it comes, even if
// the PC is off or the board was restarted in between.

typedef struct {
    uint32_t id;
    time_t at;        // when it rings, as a Unix time; 0 means this slot is free
    char label[48];
} alarm_entry_t;

// Loads what was saved. Call once after nvs_flash_init().
void alarms_init(void);

void alarms_add(uint32_t id, time_t at, const char *label);

// ids is a comma separated list from the server, e.g. "3,4".
void alarms_cancel_ids(const char *ids);

// Returns true once for each entry that has come due. Needs the clock to be set. An entry that
// was missed by more than five minutes (the board was off) is dropped without ringing.
bool alarms_pop_due(alarm_entry_t *out);

// Creates the task that does the ringing. Call once at startup, before anything can ring.
void alarms_start_ringer(void);

// The ringing: a burst of beeps once a second, for up to 45 seconds or until stopped. It pauses any
// music and puts the volume up, then puts both back.
void alarm_ring_start(const char *label);
void alarm_ring_stop(void);
bool alarm_ringing(void);
const char *alarm_ring_label(void);

#endif

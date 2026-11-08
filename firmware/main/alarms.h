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
    uint32_t repeat;  // seconds until it rings again (86400 daily, 604800 weekly); 0 rings once
    uint8_t kind;     // ALARM_KIND_*, so "cancela las alarmas" knows what to cancel
} alarm_entry_t;

#define ALARM_KIND_UNKNOWN 0
#define ALARM_KIND_TIMER 1
#define ALARM_KIND_ALARM 2
#define ALARM_KIND_REMINDER 3

// An id nobody uses yet.
uint32_t alarms_new_id(void);

// Cancels every waiting entry of that kind (0 = all of them). Returns how many were cancelled.
int alarms_cancel_kind(int kind);

// How many entries are waiting, and the one at `index` when sorted by time (0 is the soonest).
int alarms_count(void);
bool alarms_get_sorted(int index, alarm_entry_t *out);

// Loads what was saved. Call once after nvs_flash_init().
void alarms_init(void);

void alarms_add(uint32_t id, time_t at, const char *label, uint32_t repeat, uint8_t kind);

// ids is a comma separated list from the server, e.g. "3,4".
void alarms_cancel_ids(const char *ids);

// The entry that rings soonest, without removing it. False when nothing is waiting.
bool alarms_next(alarm_entry_t *out);

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

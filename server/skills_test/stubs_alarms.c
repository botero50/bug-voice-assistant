#include <stdio.h>
#include <time.h>
#include "local_skills.h"
#include "alarms.h"
#include "battery.h"

static alarm_entry_t store[8];
bool battery_read(battery_status_t *b) { b->percent = 85; b->charging = true; return true; }
uint32_t alarms_new_id(void) { static uint32_t n = 1000; return ++n; }
void alarms_add(uint32_t id, time_t at, const char *label, uint32_t repeat, uint8_t kind) {
    for (int i = 0; i < 8; i++) if (!store[i].at) { store[i].id = id; store[i].at = at; strlcpy(store[i].label, label, 48); store[i].repeat = repeat; store[i].kind = kind; return; }
}
int alarms_cancel_kind(int kind) { int n = 0; for (int i = 0; i < 8; i++) if (store[i].at && (!kind || store[i].kind == kind)) { store[i].at = 0; n++; } return n; }
int alarms_count(void) { int n = 0; for (int i = 0; i < 8; i++) if (store[i].at) n++; return n; }
bool alarms_get_sorted(int index, alarm_entry_t *out) {
    int seen = -1; time_t last = 0;
    for (int r = 0; r <= index; r++) { int best = -1; for (int i = 0; i < 8; i++) if (store[i].at && store[i].at > last && (best < 0 || store[i].at < store[best].at)) best = i; if (best < 0) return false; last = store[best].at; if (r == index) { *out = store[best]; return true; } }
    return false;
}


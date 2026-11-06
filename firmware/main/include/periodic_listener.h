#ifndef PERIODIC_LISTENER_H
#define PERIODIC_LISTENER_H

#include <stdint.h>
#include <esp_err.h>

// Periodic listening: Listen 500ms, Sleep 2s
// Average power: 2mA (vs 8mA always-listening)
// Battery life: 30+ days (vs 6-12 days)
// Wake latency: 2 seconds (vs <100ms)

esp_err_t periodic_listener_init(void);
void periodic_listener_deinit(void);
int periodic_listener_is_listening(void);
uint32_t periodic_listener_get_cycles(void);
void periodic_listener_print_stats(void);

#endif

#ifndef MICROPHONE_DUTY_CYCLE_H
#define MICROPHONE_DUTY_CYCLE_H

#include <stdint.h>
#include <stdbool.h>

// Duty cycle configuration
#define MIC_DUTY_LISTEN_MS 2000    // Listen for 2 seconds
#define MIC_DUTY_SLEEP_MS 3000     // Sleep for 3 seconds
#define MIC_DUTY_CYCLE_TOTAL_MS (MIC_DUTY_LISTEN_MS + MIC_DUTY_SLEEP_MS)

// State tracking
typedef struct {
    bool listening;
    uint32_t cycle_start_time_ms;
    uint32_t listen_duration_ms;
    uint32_t sleep_duration_ms;
    uint32_t total_cycles;
} MicrophoneDutyCycle;

// Function declarations
void mic_duty_cycle_init(uint32_t listen_ms, uint32_t sleep_ms);
void mic_duty_cycle_update(void);
bool mic_duty_cycle_should_listen(void);
MicrophoneDutyCycle mic_duty_cycle_get_state(void);
void mic_duty_cycle_force_listen(void);  // Override to always listen for duration

#endif // MICROPHONE_DUTY_CYCLE_H

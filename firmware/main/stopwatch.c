#include "stopwatch.h"
#include "esp_timer.h"
#include <string.h>

static bool running = false;
static int64_t started_us = 0;      // when it last started running
static int64_t accumulated_ms = 0;  // what was counted before the last pause

int64_t stopwatch_elapsed_ms(void) {
    return accumulated_ms + (running ? (esp_timer_get_time() - started_us) / 1000 : 0);
}

bool stopwatch_running(void) {
    return running;
}

static void start(void) {
    if (!running) {
        started_us = esp_timer_get_time();
        running = true;
    }
}

static void pause_counting(void) {
    if (running) {
        accumulated_ms = stopwatch_elapsed_ms();
        running = false;
    }
}

void stopwatch_toggle(void) {
    if (running) {
        pause_counting();
    } else {
        start();
    }
}

void stopwatch_command(const char *command) {
    if (strncmp(command, "start", 5) == 0) {
        start();
    } else if (strncmp(command, "pause", 5) == 0) {
        pause_counting();
    } else if (strncmp(command, "reset", 5) == 0) {
        running = false;
        accumulated_ms = 0;
    }
}

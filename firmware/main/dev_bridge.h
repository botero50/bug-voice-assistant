#ifndef DEV_BRIDGE_H
#define DEV_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>

// Messages from the development PC (Claude Code hooks, scripts) shown and said by the board. See docs/DEV_BRIDGE_PLAN.md.

void dev_bridge_start(void);
bool dev_bridge_paired(void);
void dev_bridge_set_token(const char *token);  // from the USB setup line; empty text removes the pairing

// The next message waiting for the screen. level: 0 info, 1 ok, 2 warn, 3 error. False when there is none.
bool dev_bridge_take(char *text, size_t size, int *level, bool *speak);

#endif

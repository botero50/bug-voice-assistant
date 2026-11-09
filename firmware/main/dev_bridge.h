#ifndef DEV_BRIDGE_H
#define DEV_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Messages from the development PC (Claude Code hooks, scripts) shown and said by the board. See docs/DEV_BRIDGE_PLAN.md.

void dev_bridge_start(void);
bool dev_bridge_paired(void);
void dev_bridge_set_relay(const char *url);  // https://... address of the internet mailbox, used away from home; empty removes it
bool dev_bridge_relay_configured(void);
void dev_bridge_set_token(const char *token);  // from the USB setup line; empty text removes the pairing

// The next message waiting for the screen. level: 0 info, 1 ok, 2 warn, 3 error. False when there is none.
// ask: the number of a permission Claude is waiting for (offer Aprobar / Rechazar), 0 for a plain message.
bool dev_bridge_take(char *text, size_t size, int *level, bool *speak, int *ask);

// Claude asked something with options: after dev_bridge_take, the labels (2 to 4, up to 47 characters) to show as buttons. Tapping one sends it
// to Claude with dev_bridge_send("say", ...), which also closes the list.
int dev_bridge_last_choices(char labels[][120]);
const char *dev_bridge_last_full(void);  // the whole text of an answer, to show on a scrollable sheet
bool dev_bridge_choice_open(void);
void dev_bridge_choice_close(void);

// A picture of the PC's screen arrived (a message said "shot"): true once, with RGB565 pixels in the display library's order, valid until the next picture.
bool dev_bridge_shot_take(const uint8_t **pixels, int *w, int *h);

int dev_bridge_pending_ask(void);                                             // the permission the screen is offering now, 0 if none
bool dev_bridge_decide(int id, bool allow);                                   // answer it (touch or voice); the bridge task sends it to the PC
bool dev_bridge_send(const char *cmd, int id, bool allow, const char *text);  // "status" or "say" (text for Claude), sent the same way

#endif

#ifndef UI_TEXT_ENTRY_H
#define UI_TEXT_ENTRY_H

#include <stdbool.h>
#include "ui_nav.h"

// Called when the user finishes. accepted is false if they swiped back instead.
typedef void (*ui_text_entry_cb)(bool accepted, const char *text);

// Opens the on-screen keyboard. The screen `return_to` is shown again afterwards, unless
// the callback already navigated somewhere else. A secret field shows dots; tap it to peek.
void ui_text_entry_open(const char *title, const char *initial, bool secret, ui_screen_id_t return_to,
                        ui_text_entry_cb callback);

// Leaves the keyboard without saving (swipe right).
void ui_text_entry_cancel(void);

#endif

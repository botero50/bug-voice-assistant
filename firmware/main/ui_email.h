#ifndef UI_EMAIL_H
#define UI_EMAIL_H

#include <stdbool.h>
#include "lvgl.h"

// The email screens. They draw what the server sends (JSON), so the same screens will
// show real mail once the accounts are connected. Opened by voice: "muéstrame los correos".

// Store the data for the next time the screen is built. False if the JSON isn't usable.
bool ui_email_set_list(const char *json);
bool ui_email_set_detail(const char *json);

bool ui_email_has_list(void);

// The newest few emails as cards; tapping a card opens that email.
lv_obj_t *ui_email_list_build(void);

// One email, sender, subject and as much of the text as fits.
lv_obj_t *ui_email_detail_build(void);

#endif // UI_EMAIL_H

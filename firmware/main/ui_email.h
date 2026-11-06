#ifndef UI_EMAIL_H
#define UI_EMAIL_H

#include "lvgl.h"

// Email screens
lv_obj_t *ui_create_email_list_screen(void);
lv_obj_t *ui_create_email_detail_screen(const char *from, const char *subject, const char *body);

#endif // UI_EMAIL_H

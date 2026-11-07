#ifndef UI_WIFI_H
#define UI_WIFI_H

#include "lvgl.h"

// Wi-Fi setup: scans for networks, shows them as a list, asks for the password
// on the keyboard, connects, and saves the network on the board when it works.
lv_obj_t *ui_wifi_build(void);

#endif

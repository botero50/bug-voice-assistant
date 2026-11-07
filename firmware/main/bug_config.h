#ifndef BUG_CONFIG_H
#define BUG_CONFIG_H

// Your Wi-Fi name, Wi-Fi password and the PC's address go in bug_config.local.h
// (copy bug_config.local.h.example). That file is ignored by Git, so the
// password never reaches GitHub. Builds without it still compile but can't
// connect.
#if __has_include("bug_config.local.h")
#include "bug_config.local.h"
#endif

#ifndef BUG_WIFI_SSID
#define BUG_WIFI_SSID ""
#endif
#ifndef BUG_WIFI_PASSWORD
#define BUG_WIFI_PASSWORD ""
#endif
#ifndef BUG_SERVER_URL
#define BUG_SERVER_URL "http://192.168.1.8:5000"
#endif

#endif

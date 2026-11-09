#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    NET_MODE_WIFI,
    NET_MODE_4G,
    NET_MODE_OFFLINE
} NetworkMode;

typedef struct {
    char ssid[33];
    int8_t rssi;   // dBm, closer to 0 is stronger
    bool secure;   // needs a password
} wifi_ap_info_t;

// Why the last connection attempt failed
typedef enum {
    NET_ERR_NONE,
    NET_ERR_PASSWORD,   // wrong password
    NET_ERR_NOT_FOUND,  // network not in range
    NET_ERR_OTHER,
} net_error_t;

esp_err_t network_manager_init(void);

// Joins a network and keeps reconnecting if it drops. Blocks up to about 15 s, and returns
// early when the password is wrong or the network isn't there.
esp_err_t network_manager_connect_wifi(const char *ssid, const char *password);

// Same, but returns at once. Poll network_manager_is_connected().
void network_manager_connect_wifi_async(const char *ssid, const char *password);
// A second network (for example the phone's hotspot), tried when the first one is not found three times in a row. Empty removes it.
void network_manager_set_backup(const char *ssid, const char *password);
bool network_manager_has_backup(void);

// Stops reconnecting and disconnects.
esp_err_t network_manager_disconnect(void);
esp_err_t network_manager_is_connected(void);
NetworkMode network_manager_get_mode(void);
net_error_t network_manager_last_error(void);

// Blocks about 2-3 s. Fills list with up to `max` networks, strongest first, one per name.
esp_err_t network_manager_scan(wifi_ap_info_t *list, int max, int *count);

esp_err_t network_manager_call_orchestrator(const char *endpoint, const char *method, const char *body, char **response);

#endif

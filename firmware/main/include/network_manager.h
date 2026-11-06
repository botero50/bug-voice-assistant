#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"

typedef enum {
    NET_MODE_WIFI,
    NET_MODE_4G,
    NET_MODE_OFFLINE
} NetworkMode;

esp_err_t network_manager_init(void);
esp_err_t network_manager_connect_wifi(const char *ssid, const char *password);
esp_err_t network_manager_disconnect(void);
esp_err_t network_manager_is_connected(void);
NetworkMode network_manager_get_mode(void);
esp_err_t network_manager_call_orchestrator(const char *endpoint, const char *method, const char *body, char **response);

#endif

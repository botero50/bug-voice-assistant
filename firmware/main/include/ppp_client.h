#ifndef PPP_CLIENT_H
#define PPP_CLIENT_H

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    PPP_STATE_IDLE,
    PPP_STATE_LINK_ESTABLISHMENT,
    PPP_STATE_AUTHENTICATION,
    PPP_STATE_NETWORK_LAYER,
    PPP_STATE_CONNECTED,
    PPP_STATE_TERMINATION,
    PPP_STATE_ERROR
} PPPState;

esp_err_t ppp_client_init(void);
esp_err_t ppp_client_start(void);
esp_err_t ppp_client_stop(void);
esp_err_t ppp_client_send_lcp_config_req(void);
esp_err_t ppp_client_send_ipcp_config_req(void);
esp_err_t ppp_client_send_echo_request(void);
esp_err_t ppp_client_set_dns(uint32_t primary_dns, uint32_t secondary_dns);
esp_err_t ppp_client_set_ip_config(uint32_t local_ip, uint32_t gateway, uint32_t primary_dns, uint32_t secondary_dns);
PPPState ppp_client_get_state(void);
uint32_t ppp_client_get_local_ip(void);
uint32_t ppp_client_get_gateway(void);
const char *ppp_client_state_to_string(PPPState state);

#endif

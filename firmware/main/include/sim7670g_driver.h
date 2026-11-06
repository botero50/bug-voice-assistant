#ifndef SIM7670G_DRIVER_H
#define SIM7670G_DRIVER_H

#include "esp_err.h"

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int rst_pin;
    int pwr_key_pin;
    uint32_t baud_rate;
} SIM7670G_Config;

typedef enum {
    SIM_STATE_INIT,
    SIM_STATE_READY,
    SIM_STATE_REGISTERED,
    SIM_STATE_CONNECTED,
    SIM_STATE_ERROR
} SIM7670G_State;

esp_err_t sim7670g_init(SIM7670G_Config *config);
esp_err_t sim7670g_power_on(void);
esp_err_t sim7670g_power_off(void);
esp_err_t sim7670g_send_at_command(const char *cmd, char *response, int max_len, int timeout_ms);
esp_err_t sim7670g_check_sim(void);
esp_err_t sim7670g_check_network(int *signal_strength);
esp_err_t sim7670g_check_registration(void);
esp_err_t sim7670g_set_apn(const char *apn);
esp_err_t sim7670g_activate_pdp_context(void);
esp_err_t sim7670g_get_ip_address(char *ip_addr, int max_len);
SIM7670G_State sim7670g_get_state(void);
const char *sim7670g_state_to_string(SIM7670G_State state);

#endif

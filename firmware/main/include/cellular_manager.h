#ifndef CELLULAR_MANAGER_H
#define CELLULAR_MANAGER_H

#include "esp_err.h"

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int rst_pin;
    int pwr_key_pin;
    uint32_t baud_rate;
} CellularConfig;

typedef enum {
    CELLULAR_STATE_INIT,
    CELLULAR_STATE_READY,
    CELLULAR_STATE_REGISTERED,
    CELLULAR_STATE_CONNECTED,
    CELLULAR_STATE_ERROR
} CellularState;

esp_err_t cellular_manager_init(CellularConfig *config);
esp_err_t cellular_manager_power_on(void);
esp_err_t cellular_manager_register_network(void);
esp_err_t cellular_manager_connect(const char *apn);
esp_err_t cellular_manager_enable_psm(uint32_t tau_seconds);
esp_err_t cellular_manager_check_signal(int *signal_strength);
esp_err_t cellular_manager_disconnect(void);
CellularState cellular_manager_get_state(void);
const char *cellular_manager_get_ip(void);
int cellular_manager_get_signal_strength(void);
const char *cellular_manager_state_to_string(CellularState state);

#endif

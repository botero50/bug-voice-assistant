#include "cellular_manager.h"
#include "sim7670g_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CELLULAR";

typedef struct {
    CellularState state;
    int signal_strength;
    char ip_address[16];
    uint32_t connection_time;
    bool psm_enabled;
    bool ota_available;
} CellularContext;

static CellularContext cellular_ctx = {
    .state = CELLULAR_STATE_INIT,
    .signal_strength = 0,
    .psm_enabled = false,
    .ota_available = false
};

esp_err_t cellular_manager_init(CellularConfig *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Cellular Manager...");

    // Initialize SIM7670G driver
    SIM7670G_Config sim_config = {
        .uart_num = config->uart_num,
        .tx_pin = config->tx_pin,
        .rx_pin = config->rx_pin,
        .rst_pin = config->rst_pin,
        .pwr_key_pin = config->pwr_key_pin,
        .baud_rate = config->baud_rate
    };

    if (sim7670g_init(&sim_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7670G driver");
        cellular_ctx.state = CELLULAR_STATE_ERROR;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✓ Cellular Manager initialized");
    cellular_ctx.state = CELLULAR_STATE_INIT;

    return ESP_OK;
}

esp_err_t cellular_manager_power_on(void) {
    ESP_LOGI(TAG, "Starting cellular connection sequence...");

    // Power on module
    if (sim7670g_power_on() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on SIM7670G");
        cellular_ctx.state = CELLULAR_STATE_ERROR;
        return ESP_FAIL;
    }

    cellular_ctx.state = CELLULAR_STATE_READY;

    // Check SIM
    if (sim7670g_check_sim() != ESP_OK) {
        ESP_LOGE(TAG, "SIM not ready");
        cellular_ctx.state = CELLULAR_STATE_ERROR;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✓ SIM card ready");

    return ESP_OK;
}

esp_err_t cellular_manager_register_network(void) {
    ESP_LOGI(TAG, "Waiting for network registration...");

    int max_retries = 60;  // 60 * 1 second = 60 seconds max
    for (int i = 0; i < max_retries; i++) {
        if (sim7670g_check_registration() == ESP_OK) {
            // Check signal strength
            int signal = 0;
            if (sim7670g_check_network(&signal) == ESP_OK) {
                cellular_ctx.signal_strength = signal;
                cellular_ctx.state = CELLULAR_STATE_REGISTERED;
                ESP_LOGI(TAG, "✓ Network registered (signal: %d/31)", signal);
                return ESP_OK;
            }
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if ((i + 1) % 10 == 0) {
            ESP_LOGD(TAG, "Waiting... %d/%d seconds", i + 1, max_retries);
        }
    }

    ESP_LOGE(TAG, "Network registration timeout");
    cellular_ctx.state = CELLULAR_STATE_ERROR;
    return ESP_ERR_TIMEOUT;
}

esp_err_t cellular_manager_connect(const char *apn) {
    if (!apn) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Activating PDP context for: %s", apn);

    // Set APN
    if (sim7670g_set_apn(apn) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APN");
        return ESP_FAIL;
    }

    // Activate PDP context
    if (sim7670g_activate_pdp_context() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate PDP context");
        cellular_ctx.state = CELLULAR_STATE_ERROR;
        return ESP_FAIL;
    }

    // Get IP address
    if (sim7670g_get_ip_address(cellular_ctx.ip_address, sizeof(cellular_ctx.ip_address)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address");
        return ESP_FAIL;
    }

    cellular_ctx.state = CELLULAR_STATE_CONNECTED;
    cellular_ctx.connection_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "✓ Cellular connected");
    ESP_LOGI(TAG, "  IP: %s", cellular_ctx.ip_address);
    ESP_LOGI(TAG, "  Signal: %d/31", cellular_ctx.signal_strength);

    return ESP_OK;
}

esp_err_t cellular_manager_enable_psm(uint32_t tau_seconds) {
    ESP_LOGI(TAG, "Enabling Power Saving Mode (TAU: %u seconds)...", tau_seconds);

    char cmd[64];
    // AT+CPSMS=<mode>,[<Requested_Periodic-RAU>],[<Requested_GPRS_READY_timer>],
    //           [<Requested_IMSI_detach_timer>],[<Requested_edrx_value>]
    snprintf(cmd, sizeof(cmd), "AT+CPSMS=1,\"\",\"\",\"%d\"", tau_seconds);

    char response[256];
    if (sim7670g_send_at_command(cmd, response, sizeof(response), 3000) != ESP_OK) {
        ESP_LOGW(TAG, "PSM enable failed or timed out");
        return ESP_FAIL;
    }

    if (strstr(response, "OK")) {
        cellular_ctx.psm_enabled = true;
        ESP_LOGI(TAG, "✓ PSM enabled");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t cellular_manager_check_signal(int *signal_strength) {
    if (cellular_ctx.state != CELLULAR_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sim7670g_check_network(signal_strength) == ESP_OK) {
        cellular_ctx.signal_strength = *signal_strength;
        return ESP_OK;
    }

    return ESP_FAIL;
}

CellularState cellular_manager_get_state(void) {
    return cellular_ctx.state;
}

const char *cellular_manager_get_ip(void) {
    if (cellular_ctx.state == CELLULAR_STATE_CONNECTED) {
        return cellular_ctx.ip_address;
    }
    return NULL;
}

int cellular_manager_get_signal_strength(void) {
    return cellular_ctx.signal_strength;
}

esp_err_t cellular_manager_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting cellular...");

    if (sim7670g_power_off() != ESP_OK) {
        ESP_LOGW(TAG, "Power off failed");
        return ESP_FAIL;
    }

    cellular_ctx.state = CELLULAR_STATE_INIT;
    memset(cellular_ctx.ip_address, 0, sizeof(cellular_ctx.ip_address));

    ESP_LOGI(TAG, "✓ Cellular disconnected");
    return ESP_OK;
}

const char *cellular_manager_state_to_string(CellularState state) {
    switch (state) {
    case CELLULAR_STATE_INIT:
        return "INIT";
    case CELLULAR_STATE_READY:
        return "READY";
    case CELLULAR_STATE_REGISTERED:
        return "REGISTERED";
    case CELLULAR_STATE_CONNECTED:
        return "CONNECTED";
    case CELLULAR_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

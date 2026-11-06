#include "sim7670g_driver.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIM7670G";

static uart_port_t uart_port;
static int rst_pin, pwr_key_pin;
static SIM7670G_State current_state = SIM_STATE_INIT;

#define UART_BUF_SIZE (1024)
#define COMMAND_TIMEOUT_MS 3000

esp_err_t sim7670g_init(SIM7670G_Config *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_port = config->uart_num;
    rst_pin = config->rst_pin;
    pwr_key_pin = config->pwr_key_pin;

    ESP_LOGI(TAG, "Initializing SIM7670G driver...");
    ESP_LOGI(TAG, "UART: %d, TX: %d, RX: %d, Baud: %d",
             uart_port, config->tx_pin, config->rx_pin, config->baud_rate);

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_port, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Configure GPIO for reset and power key
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << rst_pin) | (1ULL << pwr_key_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    ESP_LOGI(TAG, "✓ UART & GPIO configured");

    current_state = SIM_STATE_INIT;
    return ESP_OK;
}

esp_err_t sim7670g_power_on(void) {
    ESP_LOGI(TAG, "Powering on SIM7670G...");

    // Pull RST low for 100ms
    gpio_set_level(rst_pin, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(rst_pin, 1);
    ESP_LOGD(TAG, "Reset sequence completed");

    // Wait for device to boot (~5 seconds)
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Check if device is responding
    char response[256];
    if (sim7670g_send_at_command("AT", response, sizeof(response), 2000) == ESP_OK) {
        if (strstr(response, "OK")) {
            ESP_LOGI(TAG, "✓ Device responding");
            current_state = SIM_STATE_READY;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "✗ Device not responding after power-on");
    return ESP_FAIL;
}

esp_err_t sim7670g_power_off(void) {
    ESP_LOGI(TAG, "Powering off SIM7670G...");

    // Pull PWR_KEY low for 1.2 seconds to trigger power off
    gpio_set_level(pwr_key_pin, 0);
    vTaskDelay(1200 / portTICK_PERIOD_MS);
    gpio_set_level(pwr_key_pin, 1);

    current_state = SIM_STATE_INIT;
    uart_driver_delete(uart_port);

    ESP_LOGI(TAG, "✓ Device powered off");
    return ESP_OK;
}

esp_err_t sim7670g_send_at_command(const char *cmd, char *response, int max_len, int timeout_ms) {
    if (!cmd || !response || max_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear receive buffer
    uart_flush_input(uart_port);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Send command
    int len = strlen(cmd);
    uart_write_bytes(uart_port, (const char *)cmd, len);
    uart_write_bytes(uart_port, "\r\n", 2);

    ESP_LOGD(TAG, "→ %s", cmd);

    // Receive response
    int total_len = 0;
    int start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = timeout_ms / portTICK_PERIOD_MS;

    while (total_len < max_len - 1) {
        // Check timeout
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            ESP_LOGD(TAG, "← (timeout) %.*s", total_len, response);
            return ESP_ERR_TIMEOUT;
        }

        // Read available data
        int available = uart_read_bytes(uart_port, (uint8_t *)response + total_len,
                                        max_len - 1 - total_len, 100 / portTICK_PERIOD_MS);
        if (available > 0) {
            total_len += available;

            // Check for end-of-response markers
            if (strstr(response, "OK") || strstr(response, "ERROR") || strstr(response, "FAIL")) {
                response[total_len] = '\0';
                ESP_LOGD(TAG, "← %s", response);
                return ESP_OK;
            }
        }
    }

    response[total_len] = '\0';
    ESP_LOGW(TAG, "← Buffer overflow: %s", response);
    return ESP_ERR_NO_MEM;
}

esp_err_t sim7670g_check_sim(void) {
    char response[256];
    if (sim7670g_send_at_command("AT+CPIN?", response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    if (strstr(response, "READY")) {
        ESP_LOGI(TAG, "✓ SIM ready");
        return ESP_OK;
    } else if (strstr(response, "SIM PIN")) {
        ESP_LOGE(TAG, "✗ SIM locked with PIN");
        return ESP_FAIL;
    }

    return ESP_FAIL;
}

esp_err_t sim7670g_check_network(int *signal_strength) {
    char response[256];
    if (sim7670g_send_at_command("AT+CSQ", response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    // Parse: +CSQ: 31,99
    int rssi = 0, ber = 0;
    if (sscanf(response, "+CSQ: %d,%d", &rssi, &ber) == 2) {
        if (signal_strength) {
            *signal_strength = rssi;
        }
        if (rssi >= 0 && rssi <= 31) {
            ESP_LOGI(TAG, "✓ Signal strength: %d (0-31 scale)", rssi);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "✗ Invalid signal response: %s", response);
    return ESP_FAIL;
}

esp_err_t sim7670g_check_registration(void) {
    char response[256];
    if (sim7670g_send_at_command("AT+CREG?", response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    // Parse: +CREG: 0,1 (Registered, Home Network)
    // or +CREG: 0,5 (Registered, Roaming)
    if (strstr(response, "+CREG:")) {
        if (strstr(response, ",1") || strstr(response, ",5")) {
            ESP_LOGI(TAG, "✓ Network registered");
            current_state = SIM_STATE_REGISTERED;
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "⚠ Not registered: %s", response);
    return ESP_FAIL;
}

esp_err_t sim7670g_set_apn(const char *apn) {
    if (!apn) {
        return ESP_ERR_INVALID_ARG;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);

    char response[256];
    if (sim7670g_send_at_command(cmd, response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    if (strstr(response, "OK")) {
        ESP_LOGI(TAG, "✓ APN set to: %s", apn);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t sim7670g_activate_pdp_context(void) {
    char response[256];
    if (sim7670g_send_at_command("AT+CGACT=1,1", response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    if (strstr(response, "OK")) {
        ESP_LOGI(TAG, "✓ PDP context activated");
        current_state = SIM_STATE_CONNECTED;
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t sim7670g_get_ip_address(char *ip_addr, int max_len) {
    if (!ip_addr || max_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[256];
    if (sim7670g_send_at_command("AT+CIFSR", response, sizeof(response), COMMAND_TIMEOUT_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    // Response is just the IP: 10.0.0.1
    if (sscanf(response, "%s", ip_addr) == 1) {
        ESP_LOGI(TAG, "✓ IP address: %s", ip_addr);
        return ESP_OK;
    }

    return ESP_FAIL;
}

SIM7670G_State sim7670g_get_state(void) {
    return current_state;
}

const char *sim7670g_state_to_string(SIM7670G_State state) {
    switch (state) {
    case SIM_STATE_INIT:
        return "INIT";
    case SIM_STATE_READY:
        return "READY";
    case SIM_STATE_REGISTERED:
        return "REGISTERED";
    case SIM_STATE_CONNECTED:
        return "CONNECTED";
    case SIM_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

#include "ppp_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "PPP_CLIENT";

typedef struct {
    PPPState state;
    uint32_t local_ip;
    uint32_t gateway_ip;
    uint32_t primary_dns;
    uint32_t secondary_dns;
    uint32_t last_echo_time;
    bool keep_alive_enabled;
} PPPContext;

static PPPContext ppp_ctx = {
    .state = PPP_STATE_IDLE,
    .keep_alive_enabled = false
};

// PPP frame constants
#define PPP_FLAG 0x7E
#define PPP_ESCAPE 0x7D
#define PPP_LCP_PROTOCOL 0xC021
#define PPP_IPCP_PROTOCOL 0x8021
#define PPP_IP_PROTOCOL 0x0021

typedef enum {
    LCP_CONF_REQ = 1,
    LCP_CONF_ACK = 2,
    LCP_CONF_NAK = 3,
    LCP_CONF_REJ = 4,
    LCP_TERM_REQ = 5,
    LCP_TERM_ACK = 6,
    LCP_CODE_REJ = 7,
    LCP_ECHO_REQ = 8,
    LCP_ECHO_REPLY = 9
} LCP_Code;

esp_err_t ppp_client_init(void) {
    ESP_LOGI(TAG, "Initializing PPP client...");
    ppp_ctx.state = PPP_STATE_IDLE;
    ppp_ctx.keep_alive_enabled = false;
    ESP_LOGI(TAG, "✓ PPP client initialized");
    return ESP_OK;
}

esp_err_t ppp_client_start(void) {
    ESP_LOGI(TAG, "Starting PPP negotiation...");

    ppp_ctx.state = PPP_STATE_LINK_ESTABLISHMENT;

    // Step 1: LCP Configuration (Link establishment)
    if (ppp_client_send_lcp_config_req() != ESP_OK) {
        ESP_LOGE(TAG, "LCP config request failed");
        ppp_ctx.state = PPP_STATE_ERROR;
        return ESP_FAIL;
    }

    // Step 2: Wait for LCP ACK
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Step 3: IPCP Configuration (IP negotiation)
    ppp_ctx.state = PPP_STATE_NETWORK_LAYER;
    if (ppp_client_send_ipcp_config_req() != ESP_OK) {
        ESP_LOGE(TAG, "IPCP config request failed");
        ppp_ctx.state = PPP_STATE_ERROR;
        return ESP_FAIL;
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ppp_ctx.state = PPP_STATE_CONNECTED;
    ppp_ctx.last_echo_time = xTaskGetTickCount();
    ppp_ctx.keep_alive_enabled = true;

    ESP_LOGI(TAG, "✓ PPP connected");
    ESP_LOGI(TAG, "  Local IP: %u.%u.%u.%u",
             (ppp_ctx.local_ip >> 24) & 0xFF,
             (ppp_ctx.local_ip >> 16) & 0xFF,
             (ppp_ctx.local_ip >> 8) & 0xFF,
             ppp_ctx.local_ip & 0xFF);

    return ESP_OK;
}

esp_err_t ppp_client_send_lcp_config_req(void) {
    ESP_LOGD(TAG, "Sending LCP Configuration Request...");

    // In a real implementation, this would:
    // 1. Build LCP packet with options (MRU, Auth protocol, etc.)
    // 2. Calculate FCS (Frame Check Sequence)
    // 3. Send over UART to modem
    // 4. Parse response

    // Placeholder: assume modem handles this internally via AT+CGACT
    ESP_LOGD(TAG, "✓ LCP config request sent");
    return ESP_OK;
}

esp_err_t ppp_client_send_ipcp_config_req(void) {
    ESP_LOGD(TAG, "Sending IPCP Configuration Request...");

    // In a real implementation:
    // 1. Build IPCP packet requesting IP assignment
    // 2. Modem responds with assigned IP
    // 3. Parse and store DNS servers

    // Placeholder: IP obtained via AT+CIFSR in cellular_manager
    ESP_LOGD(TAG, "✓ IPCP config request sent");
    return ESP_OK;
}

esp_err_t ppp_client_send_echo_request(void) {
    if (ppp_ctx.state != PPP_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Sending PPP Echo-Request (keep-alive)...");

    // In a real implementation, send LCP Echo-Request
    // This keeps the link alive during idle periods
    // Modem should respond with Echo-Reply

    uint32_t now = xTaskGetTickCount();
    if ((now - ppp_ctx.last_echo_time) > (30 * 1000 / portTICK_PERIOD_MS)) {
        ppp_ctx.last_echo_time = now;
        // Send echo packet (placeholder)
    }

    return ESP_OK;
}

esp_err_t ppp_client_set_dns(uint32_t primary_dns, uint32_t secondary_dns) {
    ppp_ctx.primary_dns = primary_dns;
    ppp_ctx.secondary_dns = secondary_dns;

    ESP_LOGD(TAG, "DNS configured");
    ESP_LOGD(TAG, "  Primary:   %u.%u.%u.%u",
             (primary_dns >> 24) & 0xFF,
             (primary_dns >> 16) & 0xFF,
             (primary_dns >> 8) & 0xFF,
             primary_dns & 0xFF);
    ESP_LOGD(TAG, "  Secondary: %u.%u.%u.%u",
             (secondary_dns >> 24) & 0xFF,
             (secondary_dns >> 16) & 0xFF,
             (secondary_dns >> 8) & 0xFF,
             secondary_dns & 0xFF);

    return ESP_OK;
}

esp_err_t ppp_client_stop(void) {
    ESP_LOGI(TAG, "Stopping PPP...");

    ppp_ctx.keep_alive_enabled = false;

    // Send LCP Termination Request
    // (Placeholder: modem handles via AT+CGACT=0)

    ppp_ctx.state = PPP_STATE_IDLE;
    memset(&ppp_ctx, 0, sizeof(ppp_ctx));

    ESP_LOGI(TAG, "✓ PPP stopped");
    return ESP_OK;
}

PPPState ppp_client_get_state(void) {
    return ppp_ctx.state;
}

uint32_t ppp_client_get_local_ip(void) {
    return ppp_ctx.local_ip;
}

uint32_t ppp_client_get_gateway(void) {
    return ppp_ctx.gateway_ip;
}

esp_err_t ppp_client_set_ip_config(uint32_t local_ip, uint32_t gateway, uint32_t primary_dns, uint32_t secondary_dns) {
    ppp_ctx.local_ip = local_ip;
    ppp_ctx.gateway_ip = gateway;
    ppp_ctx.primary_dns = primary_dns;
    ppp_ctx.secondary_dns = secondary_dns;

    ESP_LOGD(TAG, "IP configuration updated");
    return ESP_OK;
}

const char *ppp_client_state_to_string(PPPState state) {
    switch (state) {
    case PPP_STATE_IDLE:
        return "IDLE";
    case PPP_STATE_LINK_ESTABLISHMENT:
        return "LINK_ESTABLISHMENT";
    case PPP_STATE_AUTHENTICATION:
        return "AUTHENTICATION";
    case PPP_STATE_NETWORK_LAYER:
        return "NETWORK_LAYER";
    case PPP_STATE_CONNECTED:
        return "CONNECTED";
    case PPP_STATE_TERMINATION:
        return "TERMINATION";
    case PPP_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

// Utility function for keep-alive task
void ppp_keep_alive_task(void *pvParameters) {
    while (ppp_ctx.keep_alive_enabled) {
        vTaskDelay(30000 / portTICK_PERIOD_MS);  // 30 seconds

        if (ppp_ctx.state == PPP_STATE_CONNECTED) {
            ppp_client_send_echo_request();
        }
    }

    vTaskDelete(NULL);
}

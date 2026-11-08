#include "network_manager.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "NETWORK";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAILED_BIT = BIT1;  // wrong password or network not found
static NetworkMode current_mode = NET_MODE_OFFLINE;
static bool wifi_started = false;
static volatile bool auto_connect = false;
static volatile net_error_t last_error = NET_ERR_NONE;

#define CONNECT_TIMEOUT_MS 15000

static net_error_t error_for_reason(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return NET_ERR_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return NET_ERR_NOT_FOUND;
    default:
        return NET_ERR_OTHER;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (auto_connect) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *info = (wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (current_mode == NET_MODE_WIFI) {
            current_mode = NET_MODE_OFFLINE;
        }
        if (auto_connect) {
            last_error = error_for_reason(info->reason);
            if (last_error == NET_ERR_PASSWORD || last_error == NET_ERR_NOT_FOUND) {
                xEventGroupSetBits(wifi_event_group, WIFI_FAILED_BIT);
            }
            ESP_LOGW(TAG, "WiFi disconnected (reason %d), retrying...", info->reason);
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "✓ WiFi connected: " IPSTR, IP2STR(&event->ip_info.ip));
        esp_wifi_set_ps(WIFI_PS_NONE);  // the radio sleeping between beacons added seconds to every answer
        last_error = NET_ERR_NONE;
        xEventGroupClearBits(wifi_event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        current_mode = NET_MODE_WIFI;
    }
}

esp_err_t network_manager_init(void) {
    ESP_LOGI(TAG, "Initializing network manager...");

    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    ESP_LOGI(TAG, "✓ Network manager ready");
    return ESP_OK;
}

static esp_err_t ensure_started(void) {
    if (wifi_started) {
        return ESP_OK;
    }
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_start();
    if (ret == ESP_OK) {
        wifi_started = true;
    }
    return ret;
}

void network_manager_connect_wifi_async(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    last_error = NET_ERR_NONE;
    auto_connect = true;

    bool already_started = wifi_started;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &config);
    if (!already_started) {
        ensure_started();  // the start event makes the first connection attempt
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
}

esp_err_t network_manager_connect_wifi(const char *ssid, const char *password) {
    if (!ssid || !password || !ssid[0]) {
        ESP_LOGE(TAG, "Invalid WiFi credentials");
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_connect_wifi_async(ssid, password);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & WIFI_FAILED_BIT) {
        ESP_LOGW(TAG, "WiFi connection failed: %s", last_error == NET_ERR_PASSWORD ? "wrong password" : "network not found");
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "WiFi connection timeout");
    return ESP_ERR_TIMEOUT;
}

esp_err_t network_manager_disconnect(void) {
    auto_connect = false;
    if (wifi_started) {
        esp_wifi_disconnect();
    }
    current_mode = NET_MODE_OFFLINE;
    return ESP_OK;
}

esp_err_t network_manager_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

NetworkMode network_manager_get_mode(void) {
    return current_mode;
}

net_error_t network_manager_last_error(void) {
    return last_error;
}

esp_err_t network_manager_scan(wifi_ap_info_t *list, int max, int *count) {
    *count = 0;
    esp_err_t ret = ensure_started();
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_scan_config_t scan_config = {.show_hidden = false};
    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) {
        return ESP_OK;
    }
    wifi_ap_record_t *records = heap_caps_malloc(found * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (!records) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    ret = esp_wifi_scan_get_ap_records(&found, records);
    if (ret != ESP_OK) {
        heap_caps_free(records);
        return ret;
    }

    // Strongest first, keeping one entry per name
    for (int i = 0; i < found; i++) {
        for (int j = i + 1; j < found; j++) {
            if (records[j].rssi > records[i].rssi) {
                wifi_ap_record_t swap = records[i];
                records[i] = records[j];
                records[j] = swap;
            }
        }
    }
    for (int i = 0; i < found && *count < max; i++) {
        const char *name = (const char *)records[i].ssid;
        if (name[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (int j = 0; j < *count; j++) {
            if (strcmp(list[j].ssid, name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        strlcpy(list[*count].ssid, name, sizeof(list[*count].ssid));
        list[*count].rssi = records[i].rssi;
        list[*count].secure = records[i].authmode != WIFI_AUTH_OPEN;
        (*count)++;
    }
    heap_caps_free(records);
    ESP_LOGI(TAG, "Scan found %d networks", *count);
    return ESP_OK;
}

esp_err_t network_manager_call_orchestrator(const char *endpoint, const char *method, const char *body, char **response) {
    if (current_mode == NET_MODE_OFFLINE) {
        ESP_LOGW(TAG, "No network connection available");
        return ESP_ERR_INVALID_STATE;
    }

    // Not used: the assistant talks to the PC server through voice_client.c
    ESP_LOGD(TAG, "Would call orchestrator: %s %s", method, endpoint);

    if (response) {
        *response = malloc(100);
        if (*response) {
            strcpy(*response, "{\"status\": \"mock\"}");
        }
    }

    return ESP_OK;
}

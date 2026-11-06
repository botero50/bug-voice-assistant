#include "http_cellular_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>

static const char *TAG = "HTTP_CELLULAR";

static esp_http_client_handle_t http_client = NULL;
static HttpCellularState client_state = HTTP_CELLULAR_STATE_IDLE;

esp_err_t http_cellular_client_init(HttpCellularConfig *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing HTTP Cellular Client...");

    esp_http_client_config_t http_config = {
        .url = config->base_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = config->timeout_ms,
        .buffer_size = config->buffer_size,
        .disable_auto_redirect = false,
        .use_global_ca_store = true,
    };

    http_client = esp_http_client_init(&http_config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    client_state = HTTP_CELLULAR_STATE_IDLE;
    ESP_LOGI(TAG, "✓ HTTP Cellular Client initialized");

    return ESP_OK;
}

esp_err_t http_cellular_client_request(const char *endpoint, const char *method, const char *body, char **response, size_t *response_len) {
    if (!endpoint || !response) {
        return ESP_ERR_INVALID_ARG;
    }

    if (client_state == HTTP_CELLULAR_STATE_ERROR) {
        ESP_LOGE(TAG, "Client in error state, cannot make request");
        return ESP_FAIL;
    }

    client_state = HTTP_CELLULAR_STATE_CONNECTING;

    // Build full URL
    char full_url[512];
    esp_http_client_get_url(http_client, full_url, sizeof(full_url));
    strncat(full_url, endpoint, sizeof(full_url) - strlen(full_url) - 1);

    ESP_LOGI(TAG, "HTTP %s %s", method, endpoint);

    // Set method
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    } else if (strcmp(method, "GET") == 0) {
        esp_http_client_set_method(http_client, HTTP_METHOD_GET);
    } else if (strcmp(method, "PUT") == 0) {
        esp_http_client_set_method(http_client, HTTP_METHOD_PUT);
    }

    // Set request body if present
    if (body) {
        esp_http_client_set_post_field(http_client, body, strlen(body));
        esp_http_client_set_header(http_client, "Content-Type", "application/json");
    }

    client_state = HTTP_CELLULAR_STATE_REQUEST_SENT;

    // Perform request
    esp_err_t ret = esp_http_client_perform(http_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Request failed: %s", esp_err_to_name(ret));
        client_state = HTTP_CELLULAR_STATE_ERROR;
        return ret;
    }

    int status_code = esp_http_client_get_status_code(http_client);
    ESP_LOGI(TAG, "Response status: %d", status_code);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Non-2xx response: %d", status_code);
    }

    // Read response body
    int content_length = esp_http_client_get_content_length(http_client);
    if (content_length > 0) {
        *response = malloc(content_length + 1);
        if (!*response) {
            ESP_LOGE(TAG, "Failed to allocate response buffer (%d bytes)", content_length);
            client_state = HTTP_CELLULAR_STATE_ERROR;
            return ESP_ERR_NO_MEM;
        }

        int read_len = esp_http_client_read_response(http_client, *response, content_length);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Failed to read response body");
            free(*response);
            *response = NULL;
            client_state = HTTP_CELLULAR_STATE_ERROR;
            return ESP_FAIL;
        }

        (*response)[read_len] = '\0';
        if (response_len) {
            *response_len = read_len;
        }

        ESP_LOGD(TAG, "Response body: %.*s", read_len, *response);
    }

    client_state = HTTP_CELLULAR_STATE_IDLE;

    return (status_code >= 200 && status_code < 300) ? ESP_OK : ESP_FAIL;
}

esp_err_t http_cellular_client_get(const char *endpoint, char **response, size_t *response_len) {
    return http_cellular_client_request(endpoint, "GET", NULL, response, response_len);
}

esp_err_t http_cellular_client_post(const char *endpoint, const char *body, char **response, size_t *response_len) {
    return http_cellular_client_request(endpoint, "POST", body, response, response_len);
}

esp_err_t http_cellular_client_put(const char *endpoint, const char *body, char **response, size_t *response_len) {
    return http_cellular_client_request(endpoint, "PUT", body, response, response_len);
}

esp_err_t http_cellular_client_set_header(const char *key, const char *value) {
    if (!http_client || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_http_client_set_header(http_client, key, value);
}

HttpCellularState http_cellular_client_get_state(void) {
    return client_state;
}

esp_err_t http_cellular_client_cleanup(void) {
    ESP_LOGI(TAG, "Cleaning up HTTP Cellular Client...");

    if (http_client) {
        esp_http_client_cleanup(http_client);
        http_client = NULL;
    }

    client_state = HTTP_CELLULAR_STATE_IDLE;
    ESP_LOGI(TAG, "✓ Cleanup complete");

    return ESP_OK;
}

const char *http_cellular_client_state_to_string(HttpCellularState state) {
    switch (state) {
    case HTTP_CELLULAR_STATE_IDLE:
        return "IDLE";
    case HTTP_CELLULAR_STATE_CONNECTING:
        return "CONNECTING";
    case HTTP_CELLULAR_STATE_REQUEST_SENT:
        return "REQUEST_SENT";
    case HTTP_CELLULAR_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

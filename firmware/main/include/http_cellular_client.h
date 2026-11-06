#ifndef HTTP_CELLULAR_CLIENT_H
#define HTTP_CELLULAR_CLIENT_H

#include "esp_err.h"
#include <stddef.h>

typedef struct {
    const char *base_url;
    uint32_t timeout_ms;
    size_t buffer_size;
} HttpCellularConfig;

typedef enum {
    HTTP_CELLULAR_STATE_IDLE,
    HTTP_CELLULAR_STATE_CONNECTING,
    HTTP_CELLULAR_STATE_REQUEST_SENT,
    HTTP_CELLULAR_STATE_ERROR
} HttpCellularState;

esp_err_t http_cellular_client_init(HttpCellularConfig *config);
esp_err_t http_cellular_client_request(const char *endpoint, const char *method, const char *body, char **response, size_t *response_len);
esp_err_t http_cellular_client_get(const char *endpoint, char **response, size_t *response_len);
esp_err_t http_cellular_client_post(const char *endpoint, const char *body, char **response, size_t *response_len);
esp_err_t http_cellular_client_put(const char *endpoint, const char *body, char **response, size_t *response_len);
esp_err_t http_cellular_client_set_header(const char *key, const char *value);
esp_err_t http_cellular_client_cleanup(void);
HttpCellularState http_cellular_client_get_state(void);
const char *http_cellular_client_state_to_string(HttpCellularState state);

#endif

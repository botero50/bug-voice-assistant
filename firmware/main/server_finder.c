#include "server_finder.h"
#include "esp_log.h"
#include "mdns.h"
#include "settings_store.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "FINDER";
static bool started = false;

bool server_finder_refresh(void) {
    if (!started) {
        if (mdns_init() != ESP_OK) {
            ESP_LOGW(TAG, "mDNS could not start");
            return false;
        }
        started = true;
    }
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_bugserver", "_tcp", 2000, 4, &results) != ESP_OK || !results) {
        return false;
    }
    bool found = false;
    for (mdns_result_t *r = results; r && !found; r = r->next) {
        for (mdns_ip_addr_t *a = r->addr; a && !found; a = a->next) {
            if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
            char url[64];
            snprintf(url, sizeof(url), "http://" IPSTR ":%u", IP2STR(&a->addr.u_addr.ip4), r->port);
            bug_settings_t current;
            settings_copy(&current);
            if (strcmp(current.server_url, url) != 0) {
                ESP_LOGI(TAG, "The PC is now at %s (was %s)", url, current.server_url);
                settings_set_server_url(url);
            }
            found = true;
        }
    }
    mdns_query_results_free(results);
    return found;
}

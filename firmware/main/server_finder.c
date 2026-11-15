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
    esp_err_t err = mdns_query_ptr("_bugserver", "_tcp", 3000, 4, &results);
    if (err != ESP_OK || !results) {
        ESP_LOGI(TAG, "No Bug server answered the network search (%s)", err == ESP_OK ? "no answer" : esp_err_to_name(err));
        return false;
    }
    bool found = false;
    for (mdns_result_t *r = results; r && !found; r = r->next) {
        esp_ip4_addr_t ip4 = {0};
        bool have = false;
        for (mdns_ip_addr_t *a = r->addr; a && !have; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                ip4 = a->addr.u_addr.ip4;
                have = true;
            }
        }
        if (!have && r->hostname && mdns_query_a(r->hostname, 2000, &ip4) == ESP_OK) have = true;  // the answer named the PC but did not carry its address
        ESP_LOGI(TAG, "Found \"%s\" port %u address %s", r->instance_name ? r->instance_name : "?", r->port, have ? "yes" : "no");
        if (!have) continue;
        char url[64];
        snprintf(url, sizeof(url), "http://" IPSTR ":%u", IP2STR(&ip4), r->port);
        bug_settings_t current;
        settings_copy(&current);
        if (strcmp(current.server_url, url) != 0) {
            ESP_LOGI(TAG, "The PC is now at %s (was %s)", url, current.server_url);
            settings_set_server_url(url);
        }
        found = true;
    }
    mdns_query_results_free(results);
    return found;
}

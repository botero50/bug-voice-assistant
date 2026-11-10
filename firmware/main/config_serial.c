#include "assistant.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "dev_bridge.h"
#include "mem_report.h"
#include "wake_verify.h"
#include "imu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "local_skills.h"
#include "network_manager.h"
#include "settings_store.h"
#include <stdio.h>
#include <string.h>

// Setting the board up from the installer page, over the USB cable: after flashing, the page sends one line,
//   BUGCFG {"ssid":"...","pass":"...","server":"192.168.1.4","mail":{"host":"imap.gmail.com","user":"...","password":"..."},"ics":"https://..."}
// and the board keeps each part in its own memory and answers "BUGCFG OK ...". {"status":1} lists what is set (never the
// passwords). {"forget":"wifi"|"mail"|"ics"|"all"} removes things. Nothing here is sent anywhere else.

static const char *TAG = "CONFIG";

#define LINE_BYTES 1800

static const char *text_of(cJSON *object, const char *key) {
    cJSON *item = object ? cJSON_GetObjectItem(object, key) : NULL;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static void apply(char *json_text) {
    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        printf("BUGCFG ERROR the line could not be read\n");
        return;
    }
    char done[200] = "";
    bug_settings_t settings;

    cJSON *forget = cJSON_GetObjectItem(root, "forget");
    if (cJSON_IsString(forget)) {
        bool all = strcmp(forget->valuestring, "all") == 0;
        if (all || strcmp(forget->valuestring, "wifi") == 0) {
            settings_set_wifi("", "");
            network_manager_disconnect();
            strlcat(done, " wifi-forgotten", sizeof(done));
        }
        if (all || strcmp(forget->valuestring, "mail") == 0) {
            local_mail_set_account("", "", "");
            strlcat(done, " mail-forgotten", sizeof(done));
        }
        if (all || strcmp(forget->valuestring, "ics") == 0) {
            local_calendar_set_urls("");
            strlcat(done, " calendar-forgotten", sizeof(done));
        }
    }

    if (cJSON_GetObjectItem(root, "imu")) {  // for testing the motion sensor: what it reads now
        float x = 0, y = 0, z = 0;
        bool ok = false;
        for (int i = 0; i < 40 && !ok; i++) {
            ok = imu_read(&x, &y, &z);
            if (!ok) vTaskDelay(pdMS_TO_TICKS(25));
        }
        printf("BUGCFG IMU {\"found\":%s,\"ok\":%s,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}\n", imu_available() ? "true" : "false", ok ? "true" : "false", x, y, z);
    }
    cJSON *verify = cJSON_GetObjectItem(root, "verify");  // the board's own greeting check: 0 off, 0.7 default; lower accepts more, higher fewer
    if (cJSON_IsNumber(verify)) {
        wake_verify_set_threshold((float)verify->valuedouble);
        strlcat(done, " verify", sizeof(done));
    }
    if (cJSON_GetObjectItem(root, "mem")) mem_report("asked over USB");
    const char *hear = text_of(root, "hear");  // for testing: pretend these words were heard, e.g. "tengo correos"
    if (hear && hear[0]) {
        assistant_hear(hear);
        strlcat(done, " heard", sizeof(done));
    }
    const char *open = text_of(root, "open");  // for testing: show a screen, e.g. "games?g=maze"
    if (open && open[0]) {
        assistant_open_screen(open);
        strlcat(done, " screen", sizeof(done));
    }

    const char *dev_token = text_of(root, "dev_token");
    if (dev_token) {  // the pairing token of the development PC (an empty one removes the pairing)
        dev_bridge_set_token(dev_token);
        strlcat(done, dev_token[0] ? " dev-paired" : " dev-unpaired", sizeof(done));
    }

    const char *relay = text_of(root, "relay");  // https://bug.headlessqa.com: the internet mailbox used away from home (empty removes it)
    if (relay) {
        dev_bridge_set_relay(relay);
        strlcat(done, relay[0] ? " mailbox" : " mailbox-removed", sizeof(done));
    }
    const char *ssid2 = text_of(root, "ssid2"), *pass2 = text_of(root, "pass2");  // a second Wi-Fi, e.g. the phone's hotspot
    if (ssid2) {
        network_manager_set_backup(ssid2, pass2 ? pass2 : "");
        strlcat(done, ssid2[0] ? " wifi2" : " wifi2-removed", sizeof(done));
    }
    const char *ssid = text_of(root, "ssid");
    const char *pass = text_of(root, "pass");
    if (ssid && ssid[0]) {
        settings_set_wifi(ssid, pass ? pass : "");
        network_manager_connect_wifi_async(ssid, pass ? pass : "");
        assistant_wifi_changed();
        strlcat(done, " wifi", sizeof(done));
    }
    const char *server = text_of(root, "server");
    if (server && server[0]) {
        char url[96];
        if (strstr(server, "://")) strlcpy(url, server, sizeof(url));
        else snprintf(url, sizeof(url), "http://%s%s", server, strchr(server, ':') ? "" : ":5000");
        settings_set_server_url(url);
        strlcat(done, " server", sizeof(done));
    }
    cJSON *mail = cJSON_GetObjectItem(root, "mail");
    const char *host = text_of(mail, "host"), *user = text_of(mail, "user"), *password = text_of(mail, "password");
    if (host && user && password && host[0] && user[0] && password[0]) {
        local_mail_set_account(host, user, password);
        strlcat(done, " mail", sizeof(done));
    }
    const char *ics = text_of(root, "ics");
    if (ics && ics[0]) {
        local_calendar_set_urls(ics);
        strlcat(done, " calendar", sizeof(done));
    }

    settings_copy(&settings);
    if (cJSON_GetObjectItem(root, "status")) {
        char mail_user[64] = "";
        local_mail_user(mail_user, sizeof(mail_user));
        printf("BUGCFG STATUS {\"wifi\":\"%s\",\"server\":\"%s\",\"mail\":\"%s\",\"calendar\":%s,\"dev\":%s,\"mailbox\":%s,\"wifi2\":%s,\"verify\":%.2f}\n", settings.wifi_ssid, settings.server_url, mail_user,
               local_calendar_configured() ? "true" : "false", dev_bridge_paired() ? "true" : "false",
               dev_bridge_relay_configured() ? "true" : "false", network_manager_has_backup() ? "true" : "false", wake_verify_threshold());
    } else {
        printf("BUGCFG OK%s\n", done[0] ? done : " nothing-to-change");
    }
    ESP_LOGI(TAG, "Configuration received:%s", done[0] ? done : " nothing");
    cJSON_Delete(root);
}

static void config_task(void *arg) {
    mem_task_note("config", 6144);
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 2048;
    cfg.tx_buffer_size = 512;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "The USB console could not be opened for configuration");
        vTaskDelete(NULL);
    }
    static char line[LINE_BYTES];
    size_t used = 0;
    for (;;) {
        uint8_t byte;
        if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(200)) <= 0) continue;
        if (byte == '\n' || byte == '\r') {
            if (used > 0) {
                line[used] = '\0';
                if (strncmp(line, "BUGCFG ", 7) == 0) apply(line + 7);
            }
            used = 0;
        } else if (used + 1 < sizeof(line)) {
            line[used++] = (char)byte;
        } else {
            used = 0;  // a line that is too long is dropped
        }
    }
}

void config_serial_start(void) {
    xTaskCreate(config_task, "config", 6144, NULL, 3, NULL);
}

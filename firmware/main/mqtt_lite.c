#include "mqtt_lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MQTT_HOST
#include "esp_log.h"
#include "esp_timer.h"
#include <errno.h>
#include <fcntl.h>
#include "lwip/netdb.h"
#include "lwip/sockets.h"
static const char *TAG = "MQTT";
#endif

// ------------------------------------------------------------------------------ pure part

bool mqtt_parse_url(const char *text, mqtt_url_t *url) {
    memset(url, 0, sizeof(*url));
    url->port = 1883;
    if (!text || !text[0]) return false;
    const char *p = strstr(text, "://");
    p = p ? p + 3 : text;
    const char *at = strchr(p, '@');
    if (at) {
        const char *colon = memchr(p, ':', (size_t)(at - p));
        size_t user_length = (size_t)((colon ? colon : at) - p);
        if (user_length >= sizeof(url->user)) return false;
        memcpy(url->user, p, user_length);
        if (colon) {
            size_t pass_length = (size_t)(at - colon - 1);
            if (pass_length >= sizeof(url->pass)) return false;
            memcpy(url->pass, colon + 1, pass_length);
        }
        p = at + 1;
    }
    size_t host_length = strcspn(p, ":/");
    if (host_length == 0 || host_length >= sizeof(url->host)) return false;
    memcpy(url->host, p, host_length);
    p += host_length;
    if (*p == ':') {
        int port = atoi(p + 1);
        if (port <= 0 || port > 65535) return false;
        url->port = port;
    }
    return true;
}

// The "remaining length" of a packet: 1 to 4 bytes, 7 bits each
static size_t put_length(uint8_t *out, size_t value) {
    size_t n = 0;
    do {
        uint8_t byte = value % 128;
        value /= 128;
        if (value > 0) byte |= 0x80;
        out[n++] = byte;
    } while (value > 0 && n < 4);
    return n;
}

static size_t put_string(uint8_t *out, const char *text) {
    size_t length = strlen(text);
    out[0] = (uint8_t)(length >> 8);
    out[1] = (uint8_t)length;
    memcpy(out + 2, text, length);
    return length + 2;
}

size_t mqtt_build_connect(uint8_t *out, size_t capacity, const char *client_id, const char *user, const char *pass) {
    size_t user_length = user && user[0] ? strlen(user) + 2 : 0;
    size_t pass_length = user_length && pass && pass[0] ? strlen(pass) + 2 : 0;
    size_t remaining = 10 + (strlen(client_id) + 2) + user_length + pass_length;
    uint8_t head[4];
    size_t head_size = put_length(head, remaining);
    if (1 + head_size + remaining > capacity) return 0;
    size_t o = 0;
    out[o++] = 0x10;
    memcpy(out + o, head, head_size);
    o += head_size;
    o += put_string(out + o, "MQTT");
    out[o++] = 4;  // protocol level 3.1.1
    out[o++] = (uint8_t)(0x02 | (user_length ? 0x80 : 0) | (pass_length ? 0x40 : 0));  // clean session, user name, password
    out[o++] = 0;
    out[o++] = 30;  // keep alive, seconds
    o += put_string(out + o, client_id);
    if (user_length) o += put_string(out + o, user);
    if (pass_length) o += put_string(out + o, pass);
    return o;
}

size_t mqtt_build_publish(uint8_t *out, size_t capacity, const char *topic, const char *payload, bool retain) {
    size_t payload_length = strlen(payload);
    size_t remaining = strlen(topic) + 2 + payload_length;
    uint8_t head[4];
    size_t head_size = put_length(head, remaining);
    if (1 + head_size + remaining > capacity) return 0;
    size_t o = 0;
    out[o++] = (uint8_t)(0x30 | (retain ? 1 : 0));  // QoS 0
    memcpy(out + o, head, head_size);
    o += head_size;
    o += put_string(out + o, topic);
    memcpy(out + o, payload, payload_length);
    return o + payload_length;
}

size_t mqtt_build_subscribe(uint8_t *out, size_t capacity, const char *topic) {
    size_t remaining = 2 + strlen(topic) + 2 + 1;
    uint8_t head[4];
    size_t head_size = put_length(head, remaining);
    if (1 + head_size + remaining > capacity) return 0;
    size_t o = 0;
    out[o++] = 0x82;
    memcpy(out + o, head, head_size);
    o += head_size;
    out[o++] = 0;
    out[o++] = 1;  // packet id
    o += put_string(out + o, topic);
    out[o++] = 0;  // QoS 0
    return o;
}

size_t mqtt_parse_publish(const uint8_t *data, size_t length, char *topic, size_t topic_size, char *payload, size_t payload_size, size_t *skip) {
    if (skip) *skip = 0;
    if (length < 2) return 0;
    size_t remaining = 0, multiplier = 1, i = 1;
    for (;; i++) {
        if (i >= length || i > 4) return 0;  // not complete yet (or garbage)
        remaining += (size_t)(data[i] & 0x7F) * multiplier;
        multiplier *= 128;
        if (!(data[i] & 0x80)) break;
    }
    i++;
    if (length < i + remaining) return 0;  // the rest has not arrived
    size_t total = i + remaining;
    if ((data[0] >> 4) != 3) {
        if (skip) *skip = total;  // CONNACK, SUBACK, PINGRESP...: not for us
        return 0;
    }
    if (remaining < 2) return 0;
    size_t topic_length = ((size_t)data[i] << 8) | data[i + 1];
    size_t o = i + 2;
    if (o + topic_length > total) return 0;
    size_t n = topic_length < topic_size - 1 ? topic_length : topic_size - 1;
    memcpy(topic, data + o, n);
    topic[n] = '\0';
    o += topic_length;
    if ((data[0] >> 1) & 3) o += 2;  // QoS 1 or 2 carry a packet id
    if (o > total) return 0;
    size_t body = total - o;
    n = body < payload_size - 1 ? body : payload_size - 1;
    memcpy(payload, data + o, n);
    payload[n] = '\0';
    return total;
}

// ------------------------------------------------------------------------------ network part
#ifndef MQTT_HOST

static int open_connection(const mqtt_url_t *url) {
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *found = NULL;
    char port[8];
    snprintf(port, sizeof(port), "%d", url->port);
    if (getaddrinfo(url->host, port, &hints, &found) != 0 || !found) {
        ESP_LOGW(TAG, "Cannot find %s", url->host);
        return -1;
    }
    int fd = socket(found->ai_family, found->ai_socktype, 0);
    if (fd < 0) {
        freeaddrinfo(found);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, found->ai_addr, found->ai_addrlen);
    freeaddrinfo(found);
    if (rc != 0 && errno == EINPROGRESS) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        struct timeval wait = {.tv_sec = 3, .tv_usec = 0};
        int ready = select(fd + 1, NULL, &writable, NULL, &wait);
        int error = 0;
        socklen_t size = sizeof(error);
        if (ready > 0) getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size);
        rc = (ready > 0 && error == 0) ? 0 : -1;
    }
    if (rc != 0) {
        close(fd);
        ESP_LOGW(TAG, "Cannot reach %s:%d", url->host, url->port);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);  // blocking again, with timeouts
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    return fd;
}

// Connects and logs in; returns the socket or -1
static int login(const mqtt_url_t *url) {
    int fd = open_connection(url);
    if (fd < 0) return -1;
    uint8_t packet[200];
    char client_id[24];
    snprintf(client_id, sizeof(client_id), "bug-%04x", (unsigned)(esp_timer_get_time() & 0xFFFF));
    size_t n = mqtt_build_connect(packet, sizeof(packet), client_id, url->user, url->pass);
    uint8_t answer[4] = {0};
    if (n == 0 || send(fd, packet, n, 0) != (int)n || recv(fd, answer, 4, MSG_WAITALL) != 4 || answer[0] != 0x20 || answer[3] != 0) {
        ESP_LOGW(TAG, "The broker did not accept the connection%s", (n && answer[0] == 0x20 && answer[3] == 5) ? " (wrong user or password)" : "");
        close(fd);
        return -1;
    }
    return fd;
}

bool mqtt_lite_publish(const char *text, const char *topic, const char *payload, bool retain) {
    mqtt_url_t url;
    if (!mqtt_parse_url(text, &url)) return false;
    int fd = login(&url);
    if (fd < 0) return false;
    uint8_t packet[300];
    size_t n = mqtt_build_publish(packet, sizeof(packet), topic, payload, retain);
    bool ok = n > 0 && send(fd, packet, n, 0) == (int)n;
    const uint8_t bye[2] = {0xE0, 0x00};
    send(fd, bye, 2, 0);
    close(fd);
    ESP_LOGI(TAG, "Sent %s = %s: %s", topic, payload, ok ? "ok" : "failed");
    return ok;
}

bool mqtt_lite_get(const char *text, const char *topic, char *payload, size_t payload_size, int wait_ms) {
    mqtt_url_t url;
    if (!mqtt_parse_url(text, &url)) return false;
    int fd = login(&url);
    if (fd < 0) return false;
    uint8_t packet[200];
    size_t n = mqtt_build_subscribe(packet, sizeof(packet), topic);
    bool got = false;
    if (n > 0 && send(fd, packet, n, 0) == (int)n) {
        uint8_t buffer[400];
        size_t have = 0;
        int64_t end = esp_timer_get_time() + (int64_t)wait_ms * 1000;
        while (!got && esp_timer_get_time() < end && have < sizeof(buffer)) {
            int r = recv(fd, buffer + have, sizeof(buffer) - have, 0);
            if (r > 0) have += (size_t)r;
            else if (r == 0) break;
            for (;;) {  // whole packets in the buffer
                char got_topic[96];
                size_t skip = 0;
                size_t used = mqtt_parse_publish(buffer, have, got_topic, sizeof(got_topic), payload, payload_size, &skip);
                if (used) { got = true; break; }
                if (!skip) break;
                memmove(buffer, buffer + skip, have - skip);
                have -= skip;
            }
        }
    }
    const uint8_t bye[2] = {0xE0, 0x00};
    send(fd, bye, 2, 0);
    close(fd);
    return got;
}
#endif

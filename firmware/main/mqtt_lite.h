#ifndef MQTT_LITE_H
#define MQTT_LITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A very small MQTT 3.1.1 client: connect, send one message or read one retained message, disconnect. No task, no
// background connection (the board has little internal RAM): each call opens the connection, does its job and closes it.

typedef struct {
    char host[64];
    int port;
    char user[40];
    char pass[64];
} mqtt_url_t;

// "mqtt://host", "mqtt://host:1883", "mqtt://user:password@host:1883" (a bare "host" or "host:port" also works)
bool mqtt_parse_url(const char *text, mqtt_url_t *url);

// Packet builders (pure, tested on the PC). Each returns the packet's size, or 0 if it does not fit.
size_t mqtt_build_connect(uint8_t *out, size_t capacity, const char *client_id, const char *user, const char *pass);
size_t mqtt_build_publish(uint8_t *out, size_t capacity, const char *topic, const char *payload, bool retain);
size_t mqtt_build_subscribe(uint8_t *out, size_t capacity, const char *topic);
// If the bytes start with a whole PUBLISH packet, copies its topic and payload and returns the packet's size; 0 if not (yet) one,
// or -1 style errors are reported as 0 too. `skip` is set to the size of a whole packet that is not a PUBLISH (to be dropped), else 0.
size_t mqtt_parse_publish(const uint8_t *data, size_t length, char *topic, size_t topic_size, char *payload, size_t payload_size, size_t *skip);

// Network part (the board only)
bool mqtt_lite_publish(const char *url, const char *topic, const char *payload, bool retain);
// The last retained message of a topic, waiting up to wait_ms for it; false if there is none or the broker cannot be reached
bool mqtt_lite_get(const char *url, const char *topic, char *payload, size_t payload_size, int wait_ms);

#endif

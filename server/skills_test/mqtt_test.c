// Host test of the MQTT packet code (firmware/main/mqtt_lite.c).
// Build and run:  cd server/skills_test && gcc -DMQTT_HOST mqtt_test.c ../../firmware/main/mqtt_lite.c -I../../firmware/main -o mqtt_test.exe && ./mqtt_test.exe
#include <stdio.h>
#include <string.h>
#include "mqtt_lite.h"

static int failed = 0, total = 0;
static void check(const char *name, int ok) {
    total++;
    if (!ok) { failed++; printf("FAIL: %s\n", name); }
}
static int same(const uint8_t *a, size_t n, const uint8_t *b, size_t m) { return n == m && memcmp(a, b, n) == 0; }

int main(void) {
    uint8_t out[300];
    size_t n;

    n = mqtt_build_connect(out, sizeof(out), "a", "", "");
    const uint8_t connect[] = {0x10, 0x0D, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 30, 0, 1, 'a'};
    check("connect without login", same(out, n, connect, sizeof(connect)));

    n = mqtt_build_connect(out, sizeof(out), "a", "u", "pw");
    const uint8_t login[] = {0x10, 0x14, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 30, 0, 1, 'a', 0, 1, 'u', 0, 2, 'p', 'w'};
    check("connect with user and password", same(out, n, login, sizeof(login)));

    n = mqtt_build_publish(out, sizeof(out), "t/x", "ON", false);
    const uint8_t publish[] = {0x30, 0x07, 0, 3, 't', '/', 'x', 'O', 'N'};
    check("publish", same(out, n, publish, sizeof(publish)));
    n = mqtt_build_publish(out, sizeof(out), "t/x", "ON", true);
    check("publish retained flag", n == 9 && out[0] == 0x31);

    n = mqtt_build_subscribe(out, sizeof(out), "t");
    const uint8_t subscribe[] = {0x82, 0x06, 0, 1, 0, 1, 't', 0};
    check("subscribe", same(out, n, subscribe, sizeof(subscribe)));

    uint8_t small[5];
    check("a packet that does not fit gives 0", mqtt_build_publish(small, sizeof(small), "t/x", "ON", false) == 0);

    // a long payload needs a two-byte length
    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    uint8_t large[400];
    n = mqtt_build_publish(large, sizeof(large), "t", big, false);
    check("two-byte remaining length", n == 1 + 2 + 2 + 1 + 299 && (large[1] & 0x80) && large[2] == 2);
    char topic[64], payload[400];
    size_t skip;
    check("and it parses back", mqtt_parse_publish(large, n, topic, sizeof(topic), payload, sizeof(payload), &skip) == n && strlen(payload) == 299 && strcmp(topic, "t") == 0);

    // parsing
    const uint8_t in[] = {0x31, 0x09, 0, 5, 'c', 'a', 's', 'a', '/', 'O', 'N'};
    n = mqtt_parse_publish(in, sizeof(in), topic, sizeof(topic), payload, sizeof(payload), &skip);
    check("parse a retained publish", n == 11 && strcmp(topic, "casa/") == 0 && strcmp(payload, "ON") == 0);
    const uint8_t suback[] = {0x90, 0x03, 0, 1, 0, 0x31, 0x07, 0, 3, 't', '/', 'x', 'O', 'N'};
    n = mqtt_parse_publish(suback, sizeof(suback), topic, sizeof(topic), payload, sizeof(payload), &skip);
    check("a SUBACK is skipped, not parsed", n == 0 && skip == 5);
    n = mqtt_parse_publish(suback + 5, sizeof(suback) - 5, topic, sizeof(topic), payload, sizeof(payload), &skip);
    check("then the publish", n == 9 && strcmp(topic, "t/x") == 0 && strcmp(payload, "ON") == 0);
    n = mqtt_parse_publish(suback + 5, 6, topic, sizeof(topic), payload, sizeof(payload), &skip);
    check("an incomplete packet waits", n == 0 && skip == 0);

    // addresses
    mqtt_url_t url;
    check("host only", mqtt_parse_url("192.168.1.4", &url) && strcmp(url.host, "192.168.1.4") == 0 && url.port == 1883 && !url.user[0]);
    check("scheme and port", mqtt_parse_url("mqtt://casa.local:1884", &url) && strcmp(url.host, "casa.local") == 0 && url.port == 1884);
    check("user and password", mqtt_parse_url("mqtt://bug:s3cret@10.0.0.2:1883/", &url) && strcmp(url.user, "bug") == 0 && strcmp(url.pass, "s3cret") == 0 && strcmp(url.host, "10.0.0.2") == 0);
    check("empty is refused", !mqtt_parse_url("", &url) && !mqtt_parse_url("mqtt://", &url) && !mqtt_parse_url("h:99999", &url));

    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}

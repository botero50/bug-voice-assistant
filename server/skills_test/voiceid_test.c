// Host test of the voice comparison (firmware/main/voice_id.c): the pure parts, and a sanity check on the local recordings if they exist.
// Build and run:  cd server/skills_test && gcc -DVOICE_ID_HOST voiceid_test.c ../../firmware/main/voice_id.c -I../../firmware/main -lm -o voiceid_test.exe && ./voiceid_test.exe
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice_id.h"

static int failed = 0, total = 0;
static void check(const char *name, int ok) { total++; if (!ok) { failed++; printf("FAIL: %s\n", name); } }

static int16_t *read_wav(const char *path, size_t *samples) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 44, SEEK_SET);
    *samples = (size_t)(size - 44) / 2;
    int16_t *a = malloc(*samples * 2);
    fread(a, 2, *samples, f);
    fclose(f);
    return a;
}

int main(void) {
    // similarity
    float a[VID_DIMS], b[VID_DIMS], c[VID_DIMS];
    for (int i = 0; i < VID_DIMS; i++) { a[i] = (float)i + 1; b[i] = 2 * ((float)i + 1); c[i] = (i % 2) ? 1.0f : -1.0f; }
    check("same direction is 1", fabsf(voice_id_similarity(a, b) - 1.0f) < 1e-5f);
    check("opposite is -1", fabsf(voice_id_similarity(a, (float[]){-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12}) + 1.0f) < 1e-5f);

    // too little speech
    int16_t silence[16000] = {0};
    float out[VID_DIMS];
    check("silence gives no embedding", !voice_id_embed(silence, 16000, out));
    check("a short click gives none", !voice_id_embed(silence, 800, out));

    // people
    voice_id_init();
    check("nobody at first", voice_id_count() == 0 && voice_id_identify(a, NULL) == -1);
    float pair[2 * VID_DIMS];
    memcpy(pair, a, sizeof(a));
    memcpy(pair + VID_DIMS, b, sizeof(b));
    check("enroll Ana", voice_id_enroll("Ana", pair, 2) && voice_id_count() == 1);
    check("Ana is found", voice_id_identify(a, NULL) == 0);
    check("a different vector is not", voice_id_identify(c, NULL) == -1);
    check("enroll Beto", voice_id_enroll("Beto", c, 1) && voice_id_count() == 2);
    check("Beto is found", voice_id_identify(c, NULL) == 1);
    check("enrolling the same name (any capitals) replaces", voice_id_enroll("ANA", c, 1) && voice_id_count() == 2);
    check("forget one", voice_id_forget("beto") && voice_id_count() == 1 && strcmp(voice_id_name(0), "ANA") == 0);
    check("forget an unknown name", !voice_id_forget("Zoe"));
    check("the list has a limit", voice_id_enroll("B", c, 1) && voice_id_enroll("C", c, 1) && voice_id_enroll("D", c, 1) && !voice_id_enroll("E", c, 1));
    check("forget everybody", voice_id_forget(NULL) && voice_id_count() == 0);

    // real recordings, if this PC has them (they are never committed)
    size_t n1, n2;
    int16_t *owner1 = read_wav("../wake_data/commands/bateria/20261108_094238_481468.wav", &n1);
    if (owner1) {
        float e1[VID_DIMS];
        check("a real recording gives an embedding", voice_id_embed(owner1, n1, e1));
        check("a person is like themselves", fabsf(voice_id_similarity(e1, e1) - 1.0f) < 1e-5f);
    } else {
        printf("(no local recordings: skipped)\n");
    }
    (void)n2;
    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}

// Runs the board's greeting verifier (firmware/main/wake_verify.c) on this PC over a raw 16-bit 16 kHz file and prints SCORE <probability>.
// Used by wake/check_verify.py to check the C code against the PyTorch model.
#define HOST_TEST 1
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "wake_verify.h"

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *pcm = malloc(bytes);
    size_t samples = fread(pcm, sizeof(int16_t), bytes / 2, f);
    printf("SCORE %.5f\n", wake_verify_score(pcm, samples));
    return 0;
}

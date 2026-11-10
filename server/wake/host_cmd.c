// Runs the board's command classifier (firmware/main/command_local.c) on this PC over a raw 16-bit 16 kHz file and prints what it thinks:
//   CLASS <id> <probability>
// Used by wake/onboard_two_stage_eval.py to test the on-board second check of the wake word.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "command_local.h"

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *pcm = malloc(bytes);
    size_t samples = fread(pcm, sizeof(int16_t), bytes / 2, f);
    float probability = 0;
    const char *id = command_local_classify(pcm, samples, &probability);
    printf("CLASS %s %.4f\n", id, probability);
    return 0;
}

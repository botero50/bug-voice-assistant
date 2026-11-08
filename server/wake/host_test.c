// Runs the board's detector code on this PC over a raw 16-bit 16 kHz file and prints the score of every look,
// so it can be compared with the PyTorch model (wake/check_c.py).
#include <stdio.h>
#include <stdint.h>
#include "wake_local.h"

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    int16_t frame[320];
    int index = 0;
    while (fread(frame, sizeof(int16_t), 320, f) == 320) {
        if (wake_local_feed(frame)) printf("TRIGGER at frame %d\n", index);
        float s = wake_local_last_score();
        if (s >= 0) printf("SCORE %d %.5f\n", index, s);
        index++;
    }
    return 0;
}

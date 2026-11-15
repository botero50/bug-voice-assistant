// Prints the 28 raw voice features of a 16 kHz mono 16-bit WAV (skips a 44-byte header), to compare with speaker_embed.py.
// Build:  cd server/wake && gcc -DVOICE_ID_HOST host_voiceid.c ../../firmware/main/voice_id.c -I../../firmware/main -lm -o host_voiceid.exe
#include <stdio.h>
#include <stdlib.h>
#include "voice_id.h"

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 44, SEEK_SET);
    size_t samples = (size_t)(size - 44) / 2;
    int16_t *audio = malloc(samples * 2);
    fread(audio, 2, samples, f);
    fclose(f);
    float features[VID_FEATURES];
    if (!voice_id_features(audio, samples, features)) { printf("no speech\n"); return 1; }
    for (int i = 0; i < VID_FEATURES; i++) printf("%.5f ", features[i]);
    printf("\n");
    return 0;
}

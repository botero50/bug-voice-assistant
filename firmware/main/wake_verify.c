#include "wake_verify.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "verify_model.h"
#include "wake_model.h"  // the same mel bands and window as the wake detector
#include <math.h>
#include <string.h>
#ifndef HOST_TEST
#include "nvs.h"
#endif

// The verifier has the shape of the command recogniser (command_local.c): three small convolutions and two dense layers on the log-mel features of
// 2.5 s of audio, with one output. The voice is found by energy, like server/wake/mine_windows.py does it, so the two always agree.

static const char *TAG = "VERIFY";

#define FFT_SIZE 512
#define BINS (FFT_SIZE / 2 + 1)
#define FRAME 320   // 20 ms at 16 kHz

static EXT_RAM_BSS_ATTR float features[WAKE_N_MEL][VFY_FRAMES];
static EXT_RAM_BSS_ATTR float pool1[8][12][62];
static EXT_RAM_BSS_ATTR float pool2[16][6][31];
static EXT_RAM_BSS_ATTR float pool3[32][3][15];
static EXT_RAM_BSS_ATTR float re[FFT_SIZE], im[FFT_SIZE], power_spectrum[BINS];  // external RAM: the internal one is nearly all used

static void fft(float *real, float *imag) {
    for (int i = 0, j = 0; i < FFT_SIZE; i++) {
        if (j > i) {
            float t = real[i]; real[i] = real[j]; real[j] = t;
            t = imag[i]; imag[i] = imag[j]; imag[j] = t;
        }
        int bit = FFT_SIZE >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
    }
    for (int size = 2; size <= FFT_SIZE; size <<= 1) {
        int half = size / 2;
        float angle = -2.0f * (float)M_PI / size;
        for (int start = 0; start < FFT_SIZE; start += size) {
            for (int k = 0; k < half; k++) {
                float wr = cosf(angle * k), wi = sinf(angle * k);
                int a = start + k, b = a + half;
                float tr = real[b] * wr - imag[b] * wi, ti = real[b] * wi + imag[b] * wr;
                real[b] = real[a] - tr;
                imag[b] = imag[a] - ti;
                real[a] += tr;
                imag[a] += ti;
            }
        }
    }
}

static void conv_pool(const float *in, int cin, int h, int w, const float *weights, const float *bias, int cout, float *out) {
    int oh = h / 2, ow = w / 2;
    for (int co = 0; co < cout; co++) {
        const float *kernels = &weights[co * cin * 9];
        for (int y = 0; y < oh; y++) {
            for (int x = 0; x < ow; x++) {
                float best = 0.0f;
                for (int dy = 0; dy < 2; dy++) {
                    int cy = y * 2 + dy;
                    int ky0 = cy == 0 ? 1 : 0, ky1 = cy == h - 1 ? 2 : 3;
                    for (int dx = 0; dx < 2; dx++) {
                        int cx = x * 2 + dx;
                        int kx0 = cx == 0 ? 1 : 0, kx1 = cx == w - 1 ? 2 : 3;
                        float sum = bias[co];
                        for (int ci = 0; ci < cin; ci++) {
                            const float *kernel = kernels + ci * 9;
                            const float *plane = in + ci * h * w;
                            for (int ky = ky0; ky < ky1; ky++) {
                                const float *row = plane + (cy + ky - 1) * w + (cx - 1);
                                const float *k = kernel + ky * 3;
                                for (int kx = kx0; kx < kx1; kx++) sum += k[kx] * row[kx];
                            }
                        }
                        if (sum > best) best = sum;
                    }
                }
                out[(co * oh + y) * ow + x] = best;
            }
        }
    }
}

// Where the voice begins: the first 20 ms frame whose mean level is above a quarter of the loudest one, minus 0.1 s.
static size_t voice_start(const int16_t *pcm, size_t samples) {
    size_t frames = samples / FRAME;
    float peak = 0;
    for (size_t f = 0; f < frames; f++) {
        float sum = 0;
        for (int i = 0; i < FRAME; i++) sum += (float)abs(pcm[f * FRAME + i]);
        if (sum / FRAME > peak) peak = sum / FRAME;
    }
    if (peak < 1.0f) return 0;
    for (size_t f = 0; f < frames; f++) {
        float sum = 0;
        for (int i = 0; i < FRAME; i++) sum += (float)abs(pcm[f * FRAME + i]);
        if (sum / FRAME > 0.25f * peak) return f * FRAME > 1600 ? f * FRAME - 1600 : 0;
    }
    return 0;
}

float wake_verify_score(const int16_t *pcm, size_t samples) {
    int64_t started = esp_timer_get_time();
    size_t start = voice_start(pcm, samples);
    const int16_t *voice = pcm + start;
    size_t available = samples > start ? samples - start : 0;
    double total = 0;
    for (int k = 0; k < VFY_FRAMES; k++) {
        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        for (int i = 0; i < 400; i++) {
            size_t index = (size_t)k * 320 + i;
            float s = index < available ? (float)voice[index] / 32768.0f : 0.0f;
            re[56 + i] = s * wake_hann[i];
        }
        fft(re, im);
        for (int b = 0; b < BINS; b++) power_spectrum[b] = re[b] * re[b] + im[b] * im[b];
        for (int m = 0; m < WAKE_N_MEL; m++) {
            float sum = 0;
            const float *weights = &wake_mel[m * BINS];
            for (int b = 0; b < BINS; b++) sum += weights[b] * power_spectrum[b];
            features[m][k] = logf(sum + 1e-6f);
            total += features[m][k];
        }
    }
    float mean = (float)(total / (WAKE_N_MEL * VFY_FRAMES));
    for (int m = 0; m < WAKE_N_MEL; m++) {
        for (int k = 0; k < VFY_FRAMES; k++) features[m][k] = (features[m][k] - mean) / WAKE_FEATURE_STD;
    }
    conv_pool(&features[0][0], 1, WAKE_N_MEL, VFY_FRAMES, vfy_w1, vfy_b1, 8, &pool1[0][0][0]);
    conv_pool(&pool1[0][0][0], 8, 12, 62, vfy_w2, vfy_b2, 16, &pool2[0][0][0]);
    conv_pool(&pool2[0][0][0], 16, 6, 31, vfy_w3, vfy_b3, 32, &pool3[0][0][0]);
    const float *flat = &pool3[0][0][0];  // 32 x 3 x 15 = 1440
    float logit = vfy_d2b[0];
    for (int o = 0; o < 64; o++) {
        float sum = vfy_d1b[o];
        const float *w = &vfy_d1w[o * 1440];
        for (int i = 0; i < 1440; i++) sum += w[i] * flat[i];
        if (sum > 0) logit += vfy_d2w[o] * sum;
    }
    float probability = 1.0f / (1.0f + expf(-logit));
    ESP_LOGI(TAG, "Greeting check: %.2f in %lld ms", probability, (long long)((esp_timer_get_time() - started) / 1000));
    return probability;
}

#ifndef HOST_TEST
static float threshold = -1.0f;  // -1: not read from memory yet

float wake_verify_threshold(void) {
    if (threshold < 0) {
        threshold = 0.70f;
        nvs_handle_t handle;
        if (nvs_open("bug", NVS_READONLY, &handle) == ESP_OK) {
            uint8_t percent = 0;
            if (nvs_get_u8(handle, "vfy", &percent) == ESP_OK && percent <= 100) threshold = percent / 100.0f;
            nvs_close(handle);
        }
    }
    return threshold;
}

void wake_verify_set_threshold(float value) {
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    threshold = value;
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "vfy", (uint8_t)(value * 100.0f + 0.5f));
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Greeting check: %s", value > 0 ? "on" : "off");
}
#endif

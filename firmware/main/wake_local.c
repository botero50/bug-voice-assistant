#include "wake_local.h"
#include "wake_model.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "WAKE";

#define FRAME_SAMPLES 320
#define WINDOW_SAMPLES 400
#define FFT_SIZE 512
#define BINS (FFT_SIZE / 2 + 1)
#define EVALUATE_EVERY 5          // frames: a look every 100 ms
#ifndef THRESHOLD
#define THRESHOLD 1.01f           // score needed (1.01 = never; the real value is set after tuning)
#endif
#define CONFIRMATIONS 2           // looks in a row above the threshold
#define REFRACTORY_FRAMES 100     // 2 s after a detection
#define LOG_ABOVE 0.50f

// A look with the network is cut into six steps, one per frame, so no single call takes long enough to make
// the microphone data overflow. Everything big is in the external RAM: the internal RAM is nearly all used.
static EXT_RAM_BSS_ATTR float feature_ring[WAKE_FRAMES][WAKE_N_MEL];  // log-mel of the latest frames
static EXT_RAM_BSS_ATTR float input_map[WAKE_N_MEL][WAKE_FRAMES];
static EXT_RAM_BSS_ATTR float pool1[6][12][37];
static EXT_RAM_BSS_ATTR float pool2[12][6][18];
static EXT_RAM_BSS_ATTR float pool3[24][3][9];
static float fft_re[FFT_SIZE], fft_im[FFT_SIZE], fft_power[BINS];  // small and hot: internal RAM

static int16_t tail[WINDOW_SAMPLES - FRAME_SAMPLES];  // the last 80 samples of the previous frame
static int ring_head = 0;       // where the next frame goes
static int ring_count = 0;      // how many frames are stored (up to WAKE_FRAMES)
static int frames_since_eval = 0;
static int stage = 0;           // 0: no look under way, 1..6: the next step to run
static int above_in_a_row = 0;
static int quiet_frames = 0;    // refractory countdown
static bool tables_ready = false;
static float twiddle_cos[FFT_SIZE / 2], twiddle_sin[FFT_SIZE / 2];
static uint16_t bit_reverse[FFT_SIZE];
static uint16_t mel_lo[WAKE_N_MEL], mel_hi[WAKE_N_MEL];  // the bins each band really uses
static float last_score = -1.0f;

static int64_t spent_us = 0;
static int frames_timed = 0;

static void prepare_tables(void) {
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        twiddle_cos[i] = cosf(2.0f * (float)M_PI * i / FFT_SIZE);
        twiddle_sin[i] = -sinf(2.0f * (float)M_PI * i / FFT_SIZE);
    }
    for (int i = 0; i < FFT_SIZE; i++) {
        int r = 0;
        for (int b = 0; b < 9; b++) {
            if (i & (1 << b)) r |= 1 << (8 - b);
        }
        bit_reverse[i] = (uint16_t)r;
    }
    for (int m = 0; m < WAKE_N_MEL; m++) {
        int lo = BINS, hi = 0;
        for (int k = 0; k < BINS; k++) {
            if (wake_mel[m * BINS + k] != 0.0f) {
                if (k < lo) lo = k;
                hi = k + 1;
            }
        }
        mel_lo[m] = (uint16_t)(lo < hi ? lo : 0);
        mel_hi[m] = (uint16_t)hi;
    }
    tables_ready = true;
}

// In-place complex FFT of 512 points
static void fft(float *re, float *im) {
    for (int i = 0; i < FFT_SIZE; i++) {
        int j = bit_reverse[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int size = 2; size <= FFT_SIZE; size <<= 1) {
        int half = size / 2, step = FFT_SIZE / size;
        for (int start = 0; start < FFT_SIZE; start += size) {
            for (int k = 0; k < half; k++) {
                float wr = twiddle_cos[k * step], wi = twiddle_sin[k * step];
                int a = start + k, b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

// One new frame -> one row of log-mel values in the ring
static void add_frame(const int16_t *frame) {
    memset(fft_re, 0, sizeof(fft_re));
    memset(fft_im, 0, sizeof(fft_im));
    // The 400-sample window is the previous 80 samples plus the 320 new ones, centred in 512 (56 zeros each side)
    for (int i = 0; i < WINDOW_SAMPLES; i++) {
        int16_t s = i < WINDOW_SAMPLES - FRAME_SAMPLES ? tail[i] : frame[i - (WINDOW_SAMPLES - FRAME_SAMPLES)];
        fft_re[56 + i] = ((float)s / 32768.0f) * wake_hann[i];
    }
    memcpy(tail, frame + FRAME_SAMPLES - (WINDOW_SAMPLES - FRAME_SAMPLES), sizeof(tail));

    fft(fft_re, fft_im);
    for (int k = 0; k < BINS; k++) {
        fft_power[k] = fft_re[k] * fft_re[k] + fft_im[k] * fft_im[k];
    }
    float *row = feature_ring[ring_head];
    for (int m = 0; m < WAKE_N_MEL; m++) {
        const float *weights = &wake_mel[m * BINS];
        float sum = 0;
        for (int k = mel_lo[m]; k < mel_hi[m]; k++) {
            sum += weights[k] * fft_power[k];
        }
        row[m] = logf(sum + 1e-6f);
    }
    ring_head = (ring_head + 1) % WAKE_FRAMES;
    if (ring_count < WAKE_FRAMES) ring_count++;
}

// 3x3 convolution (padding 1) of `in` [cin][h][w], then ReLU and 2x2 max pool, into out [cout][h/2][w/2]
static void conv_pool(const float *in, int cin, int h, int w, const float *weights, const float *bias, int co0, int co1,
                      float *out) {
    int oh = h / 2, ow = w / 2;
    for (int co = co0; co < co1; co++) {
        const float *kernels = &weights[co * cin * 9];
        for (int y = 0; y < oh; y++) {
            for (int x = 0; x < ow; x++) {
                float best = 0.0f;  // ReLU after the max is the same as before it
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
                                for (int kx = kx0; kx < kx1; kx++) {
                                    sum += k[kx] * row[kx];
                                }
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

// Step 1: the latest frames in time order, minus their mean (so the volume does not matter), then the first layer
static void step_input_and_conv1(void) {
    double total = 0;
    for (int t = 0; t < WAKE_FRAMES; t++) {
        const float *row = feature_ring[(ring_head + t) % WAKE_FRAMES];
        for (int m = 0; m < WAKE_N_MEL; m++) {
            input_map[m][t] = row[m];
            total += row[m];
        }
    }
    float mean = (float)(total / (WAKE_FRAMES * WAKE_N_MEL));
    for (int m = 0; m < WAKE_N_MEL; m++) {
        for (int t = 0; t < WAKE_FRAMES; t++) {
            input_map[m][t] = (input_map[m][t] - mean) / WAKE_FEATURE_STD;
        }
    }
    conv_pool(&input_map[0][0], 1, WAKE_N_MEL, WAKE_FRAMES, wake_w1, wake_b1, 0, 6, &pool1[0][0][0]);
}

static void step_conv2(int co0, int co1) {
    conv_pool(&pool1[0][0][0], 6, 12, 37, wake_w2, wake_b2, co0, co1, &pool2[0][0][0]);
}

static void step_conv3(int co0, int co1) {
    conv_pool(&pool2[0][0][0], 12, 6, 18, wake_w3, wake_b3, co0, co1, &pool3[0][0][0]);
}

static float step_dense(void) {
    const float *flat = &pool3[0][0][0];  // 24 x 3 x 9 = 648, channel first: the same order as the training
    float hidden[32];
    for (int o = 0; o < 32; o++) {
        float sum = wake_d1b[o];
        const float *w = &wake_d1w[o * 648];
        for (int i = 0; i < 648; i++) sum += w[i] * flat[i];
        hidden[o] = sum > 0 ? sum : 0;
    }
    float logit = wake_d2b[0];
    for (int o = 0; o < 32; o++) logit += wake_d2w[o] * hidden[o];
    return 1.0f / (1.0f + expf(-logit));
}

float wake_local_last_score(void) {
    return last_score;
}

void wake_local_reset(void) {
    ring_count = 0;
    ring_head = 0;
    frames_since_eval = 0;
    stage = 0;
    above_in_a_row = 0;
    memset(tail, 0, sizeof(tail));
}

bool wake_local_feed(const int16_t *frame) {
    if (!tables_ready) {
        prepare_tables();
    }
    int64_t started = esp_timer_get_time();
    add_frame(frame);
    last_score = -1.0f;
    if (quiet_frames > 0) {
        quiet_frames--;
        return false;
    }

    bool detected = false;
    if (stage == 0 && ++frames_since_eval >= EVALUATE_EVERY && ring_count >= WAKE_FRAMES) {
        frames_since_eval = 0;
        stage = 1;
    }
    if (stage == 1) {
        step_input_and_conv1();
        stage = 2;
    } else if (stage == 2) {
        step_conv2(0, 6);
        stage = 3;
    } else if (stage == 3) {
        step_conv2(6, 12);
        stage = 4;
    } else if (stage == 4) {
        step_conv3(0, 12);
        stage = 5;
    } else if (stage == 5) {
        step_conv3(12, 24);
        stage = 6;
    } else if (stage == 6) {
        stage = 0;
        float score = step_dense();
        last_score = score;
        if (score > LOG_ABOVE) {
            ESP_LOGI(TAG, "Score %.2f", score);
        }
        above_in_a_row = score > THRESHOLD ? above_in_a_row + 1 : 0;
        if (above_in_a_row >= CONFIRMATIONS) {
            above_in_a_row = 0;
            quiet_frames = REFRACTORY_FRAMES;
            ESP_LOGI(TAG, "Heard \"Hola Bug\" (score %.2f)", score);
            detected = true;
        }
    }
    spent_us += esp_timer_get_time() - started;
    if (++frames_timed % 750 == 0) {  // every 15 seconds
        ESP_LOGI(TAG, "The detector uses %lld us per frame on average", (long long)(spent_us / frames_timed));
        spent_us = 0;
        frames_timed = 0;
    }
    return detected;
}

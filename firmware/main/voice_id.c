#include "voice_id.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef VOICE_ID_HOST
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
static const char *TAG = "VOICE_ID";
#endif

#ifdef VOICE_ID_HOST
#define VID_ALLOC(n) malloc(n)
#else
#define VID_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM)  // the working buffers are about 12 KB: keep them out of the scarce internal RAM
#endif

#define RATE 16000
#define FRAME 400
#define HOP 160
#define FFT_N 512
#define BANDS 24
#define CEPS 13
#define FMIN 100.0
#define FMAX 4000.0
#define PI_D 3.14159265358979323846

// The tables are about 27 KB: made once in external RAM (static arrays would sit in the scarce internal RAM for good)
typedef struct {
    float filter[BANDS][FFT_N / 2 + 1];
    float dct[CEPS][BANDS];
    float window[FRAME];
    float cos[FFT_N / 2], sin[FFT_N / 2];
} tables_t;
static tables_t *s_t = NULL;
#define s_filter (s_t->filter)
#define s_dct (s_t->dct)
#define s_window (s_t->window)
#define s_cos (s_t->cos)
#define s_sin (s_t->sin)

static double mel_of(double f) { return 2595.0 * log10(1.0 + f / 700.0); }
static double hz_of(double m) { return 700.0 * (pow(10.0, m / 2595.0) - 1.0); }

static bool make_tables(void) {
    if (s_t) return true;
    tables_t *t = VID_ALLOC(sizeof(tables_t));
    if (!t) return false;
    s_t = t;
    double points[BANDS + 2];
    double lo = mel_of(FMIN), hi = mel_of(FMAX);
    for (int i = 0; i < BANDS + 2; i++) points[i] = hz_of(lo + (hi - lo) * i / (BANDS + 1));
    memset(s_filter, 0, sizeof(s_filter));
    for (int i = 0; i < BANDS; i++) {
        int a = (int)floor((FFT_N + 1) * points[i] / RATE), b = (int)floor((FFT_N + 1) * points[i + 1] / RATE), c = (int)floor((FFT_N + 1) * points[i + 2] / RATE);
        for (int k = a; k < b; k++) s_filter[i][k] = (float)(k - a) / (float)(b - a > 1 ? b - a : 1);
        for (int k = b; k < c; k++) s_filter[i][k] = (float)(c - k) / (float)(c - b > 1 ? c - b : 1);
    }
    for (int k = 0; k < CEPS; k++) {
        for (int n = 0; n < BANDS; n++) s_dct[k][n] = (float)(cos(PI_D * (k + 1) * (2 * n + 1) / (2.0 * BANDS)) * sqrt(2.0 / BANDS));
    }
    for (int n = 0; n < FRAME; n++) s_window[n] = (float)(0.54 - 0.46 * cos(2.0 * PI_D * n / (FRAME - 1)));
    for (int k = 0; k < FFT_N / 2; k++) {
        s_cos[k] = (float)cos(2.0 * PI_D * k / FFT_N);
        s_sin[k] = (float)-sin(2.0 * PI_D * k / FFT_N);
    }
    return true;
}

// In-place radix-2 FFT of FFT_N complex points
static void fft(float *re, float *im) {
    for (int i = 1, j = 0; i < FFT_N; i++) {
        int bit = FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        int step = FFT_N / len;
        for (int i = 0; i < FFT_N; i += len) {
            for (int k = 0; k < len / 2; k++) {
                float wr = s_cos[k * step], wi = s_sin[k * step];
                int a = i + k, b = i + k + len / 2;
                float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
        }
    }
}

static int compare_floats(const void *x, const void *y) {
    float a = *(const float *)x, b = *(const float *)y;
    return (a > b) - (a < b);
}

bool voice_id_features(const int16_t *audio, size_t samples, float *features) {
    if (samples < FRAME * 3) return false;
    if (!make_tables()) return false;
    int frames = (int)((samples - FRAME + HOP - 1) / HOP);  // range(0, n - FRAME, HOP)
    if (frames < 1) return false;
    float *energy = VID_ALLOC(sizeof(float) * (size_t)frames);
    float *f0s = VID_ALLOC(sizeof(float) * (size_t)frames);
    float *re = VID_ALLOC(sizeof(float) * (FFT_N * 2 + FRAME));
    if (!energy || !f0s || !re) {
        free(energy); free(f0s); free(re);
        return false;
    }
    float *im = re + FFT_N;
    float *frame = re + 2 * FFT_N;  // kept off the (small) task stack
    float peak = 0;
    for (int f = 0; f < frames; f++) {
        const int16_t *x = audio + (size_t)f * HOP;
        double sum = 0;
        for (int n = 0; n < FRAME; n++) {
            double v = x[n] / 32768.0;
            sum += v * v;
        }
        energy[f] = (float)sqrt(sum / FRAME);
        if (energy[f] > peak) peak = energy[f];
    }
    float floor_level = peak * 0.15f > 0.005f ? peak * 0.15f : 0.005f;
    int speech = 0;
    for (int f = 0; f < frames; f++) speech += energy[f] > floor_level;
    if (speech < 15) {
        free(energy); free(f0s); free(re);
        return false;
    }

    double sum[CEPS] = {0}, sumsq[CEPS] = {0};
    int used = 0, voiced_count = 0, speech_index = 0;
    const int lo_lag = RATE / 400, hi_lag = RATE / 60;
    for (int f = 0; f < frames; f++) {
        if (!(energy[f] > floor_level)) continue;
        const int16_t *x = audio + (size_t)f * HOP;
        for (int n = 0; n < FRAME; n++) {
            re[n] = (x[n] / 32768.0f) * s_window[n];
            im[n] = 0;
        }
        for (int n = FRAME; n < FFT_N; n++) re[n] = im[n] = 0;
        fft(re, im);
        float logmel[BANDS];
        for (int b = 0; b < BANDS; b++) {
            double acc = 0;
            for (int k = 0; k <= FFT_N / 2; k++) {
                if (s_filter[b][k] != 0) acc += s_filter[b][k] * ((double)re[k] * re[k] + (double)im[k] * im[k]);
            }
            logmel[b] = (float)log(acc + 1e-8);
        }
        for (int c = 0; c < CEPS; c++) {
            double v = 0;
            for (int b = 0; b < BANDS; b++) v += s_dct[c][b] * logmel[b];
            sum[c] += v;
            sumsq[c] += v * v;
        }
        used++;
        // pitch, on every second speech frame (the window above is not used here)
        if (speech_index++ % 2 == 0) {
            double mean = 0;
            for (int n = 0; n < FRAME; n++) mean += x[n] / 32768.0;
            mean /= FRAME;
            for (int n = 0; n < FRAME; n++) frame[n] = (float)(x[n] / 32768.0 - mean);
            double r0 = 0;
            for (int n = 0; n < FRAME; n++) r0 += (double)frame[n] * frame[n];
            if (r0 > 0) {
                double best = -1e30;
                int best_lag = lo_lag;
                for (int lag = lo_lag; lag < hi_lag; lag++) {
                    double r = 0;
                    for (int n = 0; n + lag < FRAME; n++) r += (double)frame[n] * frame[n + lag];
                    if (r > best) { best = r; best_lag = lag; }
                }
                if (best / r0 > 0.35) f0s[voiced_count++] = (float)RATE / (float)best_lag;
            }
        }
    }
    for (int c = 0; c < CEPS; c++) {
        double mean = sum[c] / used;
        double var = sumsq[c] / used - mean * mean;
        features[c] = (float)mean;
        features[CEPS + c] = (float)sqrt(var > 0 ? var : 0);
    }
    float f0 = 150.0f, spread = 0.0f;
    if (voiced_count >= 5) {
        qsort(f0s, (size_t)voiced_count, sizeof(float), compare_floats);
        f0 = voiced_count % 2 ? f0s[voiced_count / 2] : 0.5f * (f0s[voiced_count / 2 - 1] + f0s[voiced_count / 2]);
        double m = 0, v = 0;
        for (int i = 0; i < voiced_count; i++) m += f0s[i];
        m /= voiced_count;
        for (int i = 0; i < voiced_count; i++) v += (f0s[i] - m) * (f0s[i] - m);
        spread = (float)sqrt(v / voiced_count);
    }
    features[2 * CEPS] = logf(f0);
    features[2 * CEPS + 1] = spread / f0;
    free(energy); free(f0s); free(re);
    return true;
}

bool voice_id_embed(const int16_t *audio, size_t samples, float *embedding) {
    float features[VID_FEATURES];
    if (!voice_id_features(audio, samples, features)) return false;
    for (int d = 0; d < VID_DIMS; d++) {
        double v = 0;
        for (int i = 0; i < VID_FEATURES; i++) v += (double)((features[i] - VID_MEAN[i]) / VID_STD[i]) * VID_W[i * VID_DIMS + d];
        embedding[d] = (float)v;
    }
    return true;
}

float voice_id_similarity(const float *a, const float *b) {
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < VID_DIMS; i++) {
        dot += (double)a[i] * b[i];
        na += (double)a[i] * a[i];
        nb += (double)b[i] * b[i];
    }
    return (float)(dot / (sqrt(na) * sqrt(nb) + 1e-9));
}

// ------------------------------------------------------------------------------ the people

typedef struct {
    char name[VID_NAME_MAX];
    float centroid[VID_DIMS];
} person_t;

static person_t s_people[VID_MAX_PEOPLE];
static int s_count = 0;

static int same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

#ifndef VOICE_ID_HOST
static void save(void) {
    nvs_handle_t handle;
    if (nvs_open("bug", NVS_READWRITE, &handle) != ESP_OK) return;
    if (s_count == 0) {
        nvs_erase_key(handle, "vid");
    } else {
        nvs_set_blob(handle, "vid", s_people, sizeof(person_t) * (size_t)s_count);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

void voice_id_init(void) {
    nvs_handle_t handle;
    s_count = 0;
    if (nvs_open("bug", NVS_READONLY, &handle) != ESP_OK) return;
    size_t size = sizeof(s_people);
    if (nvs_get_blob(handle, "vid", s_people, &size) == ESP_OK && size % sizeof(person_t) == 0) {
        s_count = (int)(size / sizeof(person_t));
        for (int i = 0; i < s_count; i++) s_people[i].name[VID_NAME_MAX - 1] = '\0';
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "%d people known", s_count);
}
#else
static void save(void) {}
void voice_id_init(void) { s_count = 0; }
#endif

int voice_id_count(void) { return s_count; }
const char *voice_id_name(int index) { return index >= 0 && index < s_count ? s_people[index].name : ""; }

bool voice_id_enroll(const char *name, const float *embeddings, int count) {
    if (!name || !name[0] || count < 1 || !embeddings) return false;
    int slot = -1;
    for (int i = 0; i < s_count; i++) {
        if (same_name(s_people[i].name, name)) slot = i;
    }
    if (slot < 0) {
        if (s_count >= VID_MAX_PEOPLE) return false;
        slot = s_count++;
    }
    memset(&s_people[slot], 0, sizeof(person_t));
    strncpy(s_people[slot].name, name, VID_NAME_MAX - 1);
    for (int d = 0; d < VID_DIMS; d++) {
        double v = 0;
        for (int k = 0; k < count; k++) v += embeddings[k * VID_DIMS + d];
        s_people[slot].centroid[d] = (float)(v / count);
    }
    save();
    return true;
}

bool voice_id_forget(const char *name) {
    if (!name || !name[0]) {
        bool had = s_count > 0;
        s_count = 0;
        save();
        return had;
    }
    for (int i = 0; i < s_count; i++) {
        if (same_name(s_people[i].name, name)) {
            memmove(&s_people[i], &s_people[i + 1], sizeof(person_t) * (size_t)(s_count - i - 1));
            s_count--;
            save();
            return true;
        }
    }
    return false;
}

int voice_id_identify(const float *embedding, float *score) {
    int best = -1;
    float best_score = -2;
    for (int i = 0; i < s_count; i++) {
        float s = voice_id_similarity(embedding, s_people[i].centroid);
        if (s > best_score) { best_score = s; best = i; }
    }
    if (score) *score = best_score;
    return best >= 0 && best_score >= VID_THRESHOLD ? best : -1;
}

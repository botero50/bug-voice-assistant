#include "command_local.h"
#include "command_model.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wake_model.h"  // the same mel bands and window as the wake detector
#include <math.h>
#include <string.h>

static const char *TAG = "COMMAND";

#define FFT_SIZE 512
#define BINS (FFT_SIZE / 2 + 1)
#define CONFIDENCE_NEEDED 0.70f

// Command id -> the sentence the board's skills understand
static const struct { const char *id; const char *sentence; } SENTENCES[] = {
    {"hora", "que hora es"}, {"fecha", "que dia es hoy"}, {"bateria", "cuanta bateria tengo"},
    {"vol_sube", "sube el volumen"}, {"vol_baja", "baja el volumen"}, {"musica_para", "para la musica"},
    {"crono_inicia", "inicia el cronometro"}, {"crono_para", "para el cronometro"}, {"crono_cero", "reinicia el cronometro"},
    {"temp_1", "temporizador de un minuto"}, {"temp_2", "temporizador de dos minutos"},
    {"temp_5", "temporizador de cinco minutos"}, {"temp_10", "temporizador de diez minutos"},
    {"temp_15", "temporizador de quince minutos"}, {"temp_20", "temporizador de veinte minutos"},
    {"temp_30", "temporizador de treinta minutos"}, {"temp_60", "temporizador de una hora"},
    {"temp_cancela", "cancela el temporizador"}, {"alarmas_cancela", "cancela las alarmas"},
    {"alarmas_lista", "que alarmas tengo"}, {"compras", "lista de compras"}, {"compras_borra", "borra la lista de compras"},
    {"notas", "mis notas"}, {"triqui", "juguemos al triqui"}, {"inicio", "inicio"}, {"buenos_dias", "buenos dias"},
    {"clima", "como esta el clima"}, {"festivo", "cuando es el proximo festivo"}};

static EXT_RAM_BSS_ATTR float features[WAKE_N_MEL][CMD_FRAMES];
static EXT_RAM_BSS_ATTR float pool1[8][12][62];
static EXT_RAM_BSS_ATTR float pool2[16][6][31];
static EXT_RAM_BSS_ATTR float pool3[32][3][15];
static float re[FFT_SIZE], im[FFT_SIZE], power_spectrum[BINS];

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

const char *command_local_classify(const int16_t *pcm, size_t samples, float *confidence) {
    int64_t started = esp_timer_get_time();
    // Log-mel features of the first 2.5 s of the sentence, frame k covering samples 320k .. 320k+399
    double total = 0;
    for (int k = 0; k < CMD_FRAMES; k++) {
        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        for (int i = 0; i < 400; i++) {
            size_t index = (size_t)k * 320 + i;
            float s = index < samples ? (float)pcm[index] / 32768.0f : 0.0f;
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
    float mean = (float)(total / (WAKE_N_MEL * CMD_FRAMES));
    for (int m = 0; m < WAKE_N_MEL; m++) {
        for (int k = 0; k < CMD_FRAMES; k++) features[m][k] = (features[m][k] - mean) / WAKE_FEATURE_STD;
    }

    conv_pool(&features[0][0], 1, WAKE_N_MEL, CMD_FRAMES, cmd_w1, cmd_b1, 8, &pool1[0][0][0]);
    conv_pool(&pool1[0][0][0], 8, 12, 62, cmd_w2, cmd_b2, 16, &pool2[0][0][0]);
    conv_pool(&pool2[0][0][0], 16, 6, 31, cmd_w3, cmd_b3, 32, &pool3[0][0][0]);
    const float *flat = &pool3[0][0][0];  // 32 x 3 x 15 = 1440
    float hidden[64];
    for (int o = 0; o < 64; o++) {
        float sum = cmd_d1b[o];
        const float *w = &cmd_d1w[o * 1440];
        for (int i = 0; i < 1440; i++) sum += w[i] * flat[i];
        hidden[o] = sum > 0 ? sum : 0;
    }
    float logits[CMD_N_CLASSES], top = -1e30f;
    int best = 0;
    for (int c = 0; c < CMD_N_CLASSES; c++) {
        float sum = cmd_d2b[c];
        for (int o = 0; o < 64; o++) sum += cmd_d2w[c * 64 + o] * hidden[o];
        logits[c] = sum;
        if (sum > top) {
            top = sum;
            best = c;
        }
    }
    float denominator = 0;
    for (int c = 0; c < CMD_N_CLASSES; c++) denominator += expf(logits[c] - top);
    float probability = 1.0f / denominator;
    if (confidence) *confidence = probability;
    ESP_LOGI(TAG, "Command: %s (%.2f) in %lld ms", cmd_class_ids[best], probability, (long long)((esp_timer_get_time() - started) / 1000));

    return cmd_class_ids[best];
}

const char *command_local_recognize(const int16_t *pcm, size_t samples, float *confidence) {
    float probability = 0;
    const char *id = command_local_classify(pcm, samples, &probability);
    if (confidence) *confidence = probability;
    if (probability < CONFIDENCE_NEEDED || strcmp(id, "unknown") == 0 || strcmp(id, "wake") == 0) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(SENTENCES) / sizeof(SENTENCES[0]); i++) {
        if (strcmp(SENTENCES[i].id, id) == 0) return SENTENCES[i].sentence;
    }
    return NULL;
}

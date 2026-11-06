#include "speaker_identification.h"
#include <string.h>
#include <math.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "SPEAKER_ID";

// Simple MFCC-like feature extraction
typedef struct {
    float features[VOICE_EMBEDDING_DIM];
    float energy;
    float zcr;  // Zero-crossing rate
} VoiceFeatures;

typedef struct {
    char namespace_name[32];
    nvs_handle_t nvs_handle;
    SpeakerProfile profiles[MAX_SPEAKERS];
    int num_profiles;
} SpeakerIDState;

EXT_RAM_BSS_ATTR static SpeakerIDState speaker_state;

static float compute_energy(const int16_t *audio, int sample_count) {
    float energy = 0.0f;
    for (int i = 0; i < sample_count; i++) {
        float sample = audio[i] / 32768.0f;
        energy += sample * sample;
    }
    return sqrtf(energy / sample_count);
}

static float compute_zcr(const int16_t *audio, int sample_count) {
    int zero_crossings = 0;
    for (int i = 1; i < sample_count; i++) {
        if ((audio[i] >= 0 && audio[i-1] < 0) ||
            (audio[i] < 0 && audio[i-1] >= 0)) {
            zero_crossings++;
        }
    }
    return (float)zero_crossings / sample_count;
}

static void extract_simple_features(const int16_t *audio, int sample_count,
                                    VoiceFeatures *out_features) {
    // Compute energy in frequency bands (simplified MFCC)
    memset(out_features->features, 0, sizeof(out_features->features));

    // Divide into frequency bands and compute energy
    int band_size = sample_count / VOICE_EMBEDDING_DIM;
    if (band_size < 1) band_size = 1;

    for (int band = 0; band < VOICE_EMBEDDING_DIM; band++) {
        float band_energy = 0.0f;
        int start = band * band_size;
        int end = (band + 1) * band_size;
        if (end > sample_count) end = sample_count;

        for (int i = start; i < end; i++) {
            float sample = audio[i] / 32768.0f;
            band_energy += sample * sample;
        }

        out_features->features[band] = band_energy > 0 ?
            logf(band_energy + 1e-6f) : 0.0f;
    }

    // Normalize features
    float norm = 0.0f;
    for (int i = 0; i < VOICE_EMBEDDING_DIM; i++) {
        norm += out_features->features[i] * out_features->features[i];
    }
    norm = sqrtf(norm);

    if (norm > 1e-6f) {
        for (int i = 0; i < VOICE_EMBEDDING_DIM; i++) {
            out_features->features[i] /= norm;
        }
    }

    out_features->energy = compute_energy(audio, sample_count);
    out_features->zcr = compute_zcr(audio, sample_count);
}

static float compute_cosine_similarity(const float *a, const float *b) {
    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (int i = 0; i < VOICE_EMBEDDING_DIM; i++) {
        dot_product += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    norm_a = sqrtf(norm_a);
    norm_b = sqrtf(norm_b);

    if (norm_a < 1e-6f || norm_b < 1e-6f) {
        return 0.0f;
    }

    return dot_product / (norm_a * norm_b);
}

esp_err_t speaker_id_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed");
        return ret;
    }

    strcpy(speaker_state.namespace_name, "speaker_id");
    ret = nvs_open(speaker_state.namespace_name, NVS_READWRITE, &speaker_state.nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        return ret;
    }

    // Load existing profiles from NVS
    speaker_state.num_profiles = 0;
    size_t num_profiles_size = sizeof(int);
    nvs_get_blob(speaker_state.nvs_handle, "num_profiles", &speaker_state.num_profiles,
                 &num_profiles_size);

    for (int i = 0; i < speaker_state.num_profiles && i < MAX_SPEAKERS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "profile_%d", i);
        size_t profile_size = sizeof(SpeakerProfile);
        nvs_get_blob(speaker_state.nvs_handle, key, &speaker_state.profiles[i],
                     &profile_size);
    }

    ESP_LOGI(TAG, "Speaker ID initialized with %d profiles", speaker_state.num_profiles);
    return ESP_OK;
}

esp_err_t speaker_id_enroll(const char *user_name, const int16_t *audio,
                            int sample_count) {
    if (speaker_state.num_profiles >= MAX_SPEAKERS) {
        ESP_LOGE(TAG, "Maximum speakers reached (%d)", MAX_SPEAKERS);
        return ESP_ERR_NO_MEM;
    }

    if (sample_count < 16000) {  // Minimum 1 second @ 16kHz
        ESP_LOGE(TAG, "Insufficient audio for enrollment");
        return ESP_ERR_INVALID_ARG;
    }

    // Extract voice features
    VoiceFeatures features;
    extract_simple_features(audio, sample_count, &features);

    // Create new profile
    SpeakerProfile *profile = &speaker_state.profiles[speaker_state.num_profiles];

    snprintf(profile->speaker_id, sizeof(profile->speaker_id), "sp_%03d",
             speaker_state.num_profiles + 1);
    strncpy(profile->name, user_name, sizeof(profile->name) - 1);
    memcpy(profile->voice_embedding, features.features,
           sizeof(profile->voice_embedding));
    profile->confidence_threshold = 0.85f;
    profile->enrollment_timestamp = (uint32_t)time(NULL);
    strcpy(profile->language, "es-CO");
    strcpy(profile->timezone, "America/Bogota");

    speaker_state.num_profiles++;

    // Save to NVS
    char key[32];
    snprintf(key, sizeof(key), "profile_%d", speaker_state.num_profiles - 1);
    nvs_set_blob(speaker_state.nvs_handle, key, profile, sizeof(SpeakerProfile));
    nvs_set_blob(speaker_state.nvs_handle, "num_profiles", &speaker_state.num_profiles,
                 sizeof(int));
    nvs_commit(speaker_state.nvs_handle);

    ESP_LOGI(TAG, "Enrolled speaker: %s (ID: %s)", user_name, profile->speaker_id);
    return ESP_OK;
}

int speaker_id_identify(const int16_t *audio, int sample_count,
                        SpeakerIdentificationResult *out_result) {
    if (speaker_state.num_profiles == 0) {
        ESP_LOGW(TAG, "No enrolled speakers");
        strcpy(out_result->speaker_id, "UNKNOWN");
        strcpy(out_result->speaker_name, "Unknown");
        out_result->confidence = 0.0f;
        return 0;
    }

    // Extract features from input audio
    VoiceFeatures input_features;
    extract_simple_features(audio, sample_count, &input_features);

    float best_similarity = 0.0f;
    int best_match_idx = -1;

    // Compare against all enrolled speakers
    for (int i = 0; i < speaker_state.num_profiles; i++) {
        float similarity = compute_cosine_similarity(
            input_features.features,
            speaker_state.profiles[i].voice_embedding
        );

        ESP_LOGD(TAG, "Speaker %s: similarity=%.3f",
                 speaker_state.profiles[i].name, similarity);

        if (similarity > best_similarity) {
            best_similarity = similarity;
            best_match_idx = i;
        }
    }

    // Check confidence threshold
    if (best_match_idx >= 0 && best_similarity >= 0.85f) {
        SpeakerProfile *matched = &speaker_state.profiles[best_match_idx];
        strcpy(out_result->speaker_id, matched->speaker_id);
        strcpy(out_result->speaker_name, matched->name);
        out_result->confidence = best_similarity;
        out_result->timestamp = (uint32_t)time(NULL);

        ESP_LOGI(TAG, "Identified: %s (confidence: %.2f)", matched->name, best_similarity);
        return 1;
    }

    // Unknown or below threshold
    strcpy(out_result->speaker_id, "UNKNOWN");
    strcpy(out_result->speaker_name, "Unknown Speaker");
    out_result->confidence = best_similarity;
    out_result->timestamp = (uint32_t)time(NULL);

    ESP_LOGI(TAG, "Unknown speaker (best match: %.2f)", best_similarity);
    return 0;
}

esp_err_t speaker_id_get_profile(const char *speaker_id, SpeakerProfile *out_profile) {
    for (int i = 0; i < speaker_state.num_profiles; i++) {
        if (strcmp(speaker_state.profiles[i].speaker_id, speaker_id) == 0) {
            memcpy(out_profile, &speaker_state.profiles[i], sizeof(SpeakerProfile));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t speaker_id_delete_speaker(const char *speaker_id) {
    int delete_idx = -1;

    for (int i = 0; i < speaker_state.num_profiles; i++) {
        if (strcmp(speaker_state.profiles[i].speaker_id, speaker_id) == 0) {
            delete_idx = i;
            break;
        }
    }

    if (delete_idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    // Remove from array
    for (int i = delete_idx; i < speaker_state.num_profiles - 1; i++) {
        memcpy(&speaker_state.profiles[i], &speaker_state.profiles[i + 1],
               sizeof(SpeakerProfile));
    }
    speaker_state.num_profiles--;

    // Save to NVS
    nvs_set_blob(speaker_state.nvs_handle, "num_profiles", &speaker_state.num_profiles,
                 sizeof(int));
    for (int i = 0; i < speaker_state.num_profiles; i++) {
        char key[32];
        snprintf(key, sizeof(key), "profile_%d", i);
        nvs_set_blob(speaker_state.nvs_handle, key, &speaker_state.profiles[i],
                     sizeof(SpeakerProfile));
    }
    nvs_commit(speaker_state.nvs_handle);

    ESP_LOGI(TAG, "Deleted speaker profile: %s", speaker_id);
    return ESP_OK;
}

int speaker_id_get_profile_count(void) {
    return speaker_state.num_profiles;
}

void speaker_id_print_profiles(void) {
    ESP_LOGI(TAG, "\n╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Enrolled Speaker Profiles                  ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════╣");

    for (int i = 0; i < speaker_state.num_profiles; i++) {
        SpeakerProfile *p = &speaker_state.profiles[i];
        ESP_LOGI(TAG, "║ %d. %s (%s)", i + 1, p->name, p->speaker_id);
        ESP_LOGI(TAG, "║    Language: %s, TZ: %s", p->language, p->timezone);
    }

    ESP_LOGI(TAG, "║ Total: %d/%d profiles", speaker_state.num_profiles, MAX_SPEAKERS);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝\n");
}

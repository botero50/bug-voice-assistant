#ifndef SPEAKER_IDENTIFICATION_H
#define SPEAKER_IDENTIFICATION_H

#include <stdint.h>
#include <esp_err.h>
#include <time.h>

#define MAX_SPEAKERS 10
#define MAX_SPEAKER_NAME 32
#define VOICE_EMBEDDING_DIM 256

typedef struct {
    char speaker_id[16];
    char name[MAX_SPEAKER_NAME];
    float voice_embedding[VOICE_EMBEDDING_DIM];
    float confidence_threshold;
    uint32_t enrollment_timestamp;
    char language[8];   // "es-CO", "en-US"
    char timezone[32];  // "America/Bogota"
} SpeakerProfile;

typedef struct {
    char speaker_id[16];
    char speaker_name[MAX_SPEAKER_NAME];
    float confidence;  // 0.0-1.0
    uint32_t timestamp;
} SpeakerIdentificationResult;

esp_err_t speaker_id_init(void);
esp_err_t speaker_id_enroll(const char *user_name, const int16_t *audio, int sample_count);
int speaker_id_identify(const int16_t *audio, int sample_count, SpeakerIdentificationResult *out);
esp_err_t speaker_id_get_profile(const char *speaker_id, SpeakerProfile *out_profile);
esp_err_t speaker_id_delete_speaker(const char *speaker_id);
int speaker_id_get_profile_count(void);
void speaker_id_print_profiles(void);

#endif

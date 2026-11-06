#include "voice_loop.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_driver.h"
#include "microsd_manager.h"
#include "network_manager.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "VOICE_LOOP";
static AudioBuffer audio_buffer;
static bool capturing = false;

void voice_loop_init(void) {
    ESP_LOGI(TAG, "Initializing voice loop buffer (%d bytes)", AUDIO_BUFFER_SIZE * sizeof(int16_t));
    audio_buffer.buffer = (int16_t *)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audio_buffer.buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }
    audio_buffer.buffer_size = AUDIO_BUFFER_SIZE;
    audio_buffer.write_pos = 0;
    audio_buffer.valid_samples = 0;
    ESP_LOGI(TAG, "✓ Voice loop ready");
}

void voice_loop_start_capture(void) {
    if (capturing) {
        ESP_LOGW(TAG, "Capture already in progress");
        return;
    }

    ESP_LOGI(TAG, "Starting audio capture...");
    audio_buffer.write_pos = 0;
    audio_buffer.valid_samples = 0;

    if (audio_start_recording() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start recording");
        return;
    }

    capturing = true;
    ESP_LOGI(TAG, "✓ Capture started (max 10 seconds)");
}

void voice_loop_stop_capture(void) {
    if (!capturing) {
        ESP_LOGW(TAG, "Capture not in progress");
        return;
    }

    capturing = false;
    if (audio_stop_recording() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop recording");
    }
    ESP_LOGI(TAG, "✓ Capture stopped (%d samples collected)", audio_buffer.valid_samples);
}

int voice_loop_get_audio_data(int16_t **buffer, int *buffer_size) {
    if (audio_buffer.valid_samples == 0) {
        return 0;
    }

    *buffer = audio_buffer.buffer;
    *buffer_size = audio_buffer.valid_samples;
    return 1;
}

static void voice_activity_detection(void) {
    // Placeholder for VAD logic
    // Would analyze energy/RMS of samples to detect speech
    ESP_LOGD(TAG, "VAD: %d samples analyzed", audio_buffer.valid_samples);
}

static void process_audio_stt(void) {
    ESP_LOGI(TAG, "Processing audio with STT...");

    // TODO: Load whisper model from MicroSD
    // ModelInfo whisper_info;
    // if (microsd_check_model("whisper_base", &whisper_info) != ESP_OK) {
    //     ESP_LOGW(TAG, "STT model not available, falling back to cloud");
    //     return;
    // }

    // For now, mock response
    ESP_LOGI(TAG, "✓ STT (mock): 'Hola Bug, ¿cómo estás?'");
}

static void process_llm(void) {
    ESP_LOGI(TAG, "Processing with LLM...");

    // TODO: Call local LLM or cloud Claude API via network
    // Could be:
    // 1. Local TinyLLaMA from MicroSD
    // 2. Cloud Claude API via WiFi/4G

    ESP_LOGI(TAG, "✓ LLM (mock): 'Hola! Estoy bien, ¿cómo puedo ayudarte?'");
}

static void process_tts(void) {
    ESP_LOGI(TAG, "Processing audio synthesis...");

    // TODO: Load piper TTS model from MicroSD
    // ModelInfo tts_info;
    // if (microsd_check_model("piper_es_CO", &tts_info) != ESP_OK) {
    //     ESP_LOGW(TAG, "TTS model not available, skipping audio synthesis");
    //     return;
    // }

    ESP_LOGI(TAG, "✓ TTS (mock): Synthesizing response...");
    // TODO: Play audio via speaker
}

void voice_loop_process(void) {
    if (audio_buffer.valid_samples == 0) {
        ESP_LOGW(TAG, "No audio data to process");
        return;
    }

    ESP_LOGI(TAG, "\n╔═══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Processing voice input (%.1f seconds)       ║",
             audio_buffer.valid_samples / (float)AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════╝");

    // Voice Activity Detection
    voice_activity_detection();

    // STT: Speech-to-Text
    process_audio_stt();

    // LLM: Language Model
    process_llm();

    // TTS: Text-to-Speech
    process_tts();

    ESP_LOGI(TAG, "✓ Voice processing complete\n");
}

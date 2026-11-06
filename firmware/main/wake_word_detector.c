#include "wake_word_detector.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "WAKE_WORD";

static WakeWordContext wwd_ctx = {
    .state = WAKE_WORD_LISTENING,
    .confidence = 0.0,
    .false_positive_count = 0,
    .true_positive_count = 0
};

#define WAKE_WORD_CONFIDENCE_THRESHOLD 0.85
#define AUDIO_FRAME_SIZE 4000  // 250ms @ 16kHz

esp_err_t wake_word_detector_init(void) {
    ESP_LOGI(TAG, "Initializing wake word detector...");
    
    // In production: Load TensorFlow Lite model from MicroSD
    // For now: Energy-based detection (simple fallback)
    
    wwd_ctx.state = WAKE_WORD_LISTENING;
    wwd_ctx.confidence = 0.0;
    
    ESP_LOGI(TAG, "✓ Wake word detector ready");
    ESP_LOGI(TAG, "  Listening for: 'Hola Bug'");
    ESP_LOGI(TAG, "  Confidence threshold: %.2f", WAKE_WORD_CONFIDENCE_THRESHOLD);
    
    return ESP_OK;
}

static float calculate_confidence(const int16_t *audio_frame, int frame_size) {
    // Energy-based voice activity detection
    // In production: would run TensorFlow Lite model
    
    int64_t energy = 0;
    for (int i = 0; i < frame_size; i++) {
        energy += (int64_t)audio_frame[i] * audio_frame[i];
    }
    
    float rms = sqrt((float)energy / frame_size) / 32768.0;
    
    // Map RMS to confidence (0-1)
    // Higher RMS = likely speech = higher confidence for "Hola Bug"
    float confidence = fmin(1.0, rms * 2.0);  // Scale by 2x
    
    return confidence;
}

esp_err_t wake_word_detector_process(const int16_t *audio_frame, int frame_size) {
    if (!audio_frame || frame_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate confidence
    float confidence = calculate_confidence(audio_frame, frame_size);
    wwd_ctx.confidence = confidence;
    
    if (wwd_ctx.state == WAKE_WORD_LISTENING) {
        if (confidence >= WAKE_WORD_CONFIDENCE_THRESHOLD) {
            // WAKE WORD DETECTED!
            wwd_ctx.state = WAKE_WORD_DETECTED;
            wwd_ctx.last_detection_time = xTaskGetTickCount();
            wwd_ctx.true_positive_count++;
            
            ESP_LOGI(TAG, "🎙️  WAKE WORD DETECTED!");
            ESP_LOGI(TAG, "  Confidence: %.2f", confidence);
            ESP_LOGI(TAG, "  Total detections: %u", wwd_ctx.true_positive_count);
            
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

WakeWordState wake_word_detector_get_state(void) {
    return wwd_ctx.state;
}

float wake_word_detector_get_confidence(void) {
    return wwd_ctx.confidence;
}

void wake_word_detector_reset(void) {
    wwd_ctx.state = WAKE_WORD_LISTENING;
    wwd_ctx.confidence = 0.0;
    ESP_LOGI(TAG, "Wake word detector reset, listening again...");
}

static void wake_word_detection_task(void *pvParameters) {
    ESP_LOGI(TAG, "Wake word detection task started");
    
    // This task would run continuously on the device
    // listening for the wake word in background audio
    
    while (1) {
        if (wwd_ctx.state == WAKE_WORD_LISTENING) {
            // Continuously monitor audio for wake word
            // In real implementation:
            // 1. Capture 250ms audio frame
            // 2. Run through TensorFlow model
            // 3. If confidence > threshold, trigger capture
            
            vTaskDelay(250 / portTICK_PERIOD_MS);
        } else if (wwd_ctx.state == WAKE_WORD_DETECTED) {
            // Wake word detected, wait for voice loop to finish
            // Then reset back to listening
            vTaskDelay(100 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}

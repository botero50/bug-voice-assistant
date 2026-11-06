#include "audio_driver.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static uint8_t current_volume = 100;

// I2S pins to the board's ES8311 codec (same map as TamaPoke's pin_config.h).
// The codec itself still needs configuring over I2C before audio will work.
#define I2S_PIN_MCLK 42
#define I2S_PIN_BCLK 9
#define I2S_PIN_WS 45
#define I2S_PIN_DOUT 8
#define I2S_PIN_DIN 10

esp_err_t audio_init(AudioDirection direction) {
    ESP_LOGI(TAG, "Initializing I2S audio (direction=%d)...", direction);

    bool use_rx = direction == AUDIO_DIR_IN || direction == AUDIO_DIR_DUPLEX;
    bool use_tx = direction == AUDIO_DIR_OUT || direction == AUDIO_DIR_DUPLEX;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, use_tx ? &tx_handle : NULL, use_rx ? &rx_handle : NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),  // 16kHz sample rate
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_PIN_MCLK,
            .bclk = I2S_PIN_BCLK,
            .ws = I2S_PIN_WS,
            .dout = use_tx ? I2S_PIN_DOUT : I2S_GPIO_UNUSED,
            .din = use_rx ? I2S_PIN_DIN : I2S_GPIO_UNUSED,
        },
    };

    if (use_rx) {
        ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize RX channel: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    if (use_tx) {
        ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize TX channel: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "✓ Audio driver ready (16kHz, 16-bit, mono)");
    return ESP_OK;
}

esp_err_t audio_start_recording(void) {
    if (!rx_handle) {
        ESP_LOGE(TAG, "RX channel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "✓ Recording started");
    return ESP_OK;
}

esp_err_t audio_stop_recording(void) {
    if (!rx_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_disable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable RX: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "✓ Recording stopped");
    return ESP_OK;
}

esp_err_t audio_read_samples(int16_t *buffer, size_t buffer_size, size_t *read_size) {
    if (!rx_handle || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2s_channel_read(rx_handle, buffer, buffer_size, read_size, pdMS_TO_TICKS(100));
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Failed to read audio: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t audio_write_samples(const int16_t *buffer, size_t buffer_size) {
    if (!tx_handle || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t written = 0;
    esp_err_t ret = i2s_channel_write(tx_handle, buffer, buffer_size, &written, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write audio: %s", esp_err_to_name(ret));
        return ret;
    }

    if (written != buffer_size) {
        ESP_LOGW(TAG, "Partial write: %u / %u bytes", written, buffer_size);
    }

    return ESP_OK;
}

esp_err_t audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;

    current_volume = volume;

    // TODO: Implement actual volume control via DAC or PWM amplifier
    // For now, just track the value
    ESP_LOGD(TAG, "Volume set to %d%%", volume);

    return ESP_OK;
}

uint8_t audio_get_volume(void) {
    return current_volume;
}

esp_err_t audio_deinit(void) {
    if (rx_handle) {
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }

    if (tx_handle) {
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }

    ESP_LOGI(TAG, "Audio driver deinitialized");
    return ESP_OK;
}

#include "audio_driver.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static uint8_t current_volume = 100;

// Board wiring (same map as TamaPoke's pin_config.h)
#define I2S_PIN_MCLK 42
#define I2S_PIN_BCLK 9
#define I2S_PIN_WS 45
#define I2S_PIN_DOUT 8
#define I2S_PIN_DIN 10
#define PIN_SPEAKER_AMP 46

#define SAMPLE_RATE 16000
#define I2C_PORT I2C_NUM_0
#define I2C_TIMEOUT pdMS_TO_TICKS(50)
#define ES8311_ADDR 0x18  // Speaker output
#define ES7210_ADDR 0x40  // Microphones

// I2S always carries two channels; the public API is mono.
#define CHUNK_FRAMES 256

static esp_err_t reg_write(uint8_t addr, uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT, addr, data, sizeof(data), I2C_TIMEOUT);
}

static esp_err_t reg_read(uint8_t addr, uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, value, 1, I2C_TIMEOUT);
}

typedef struct {
    uint8_t reg;
    uint8_t value;
} reg_value_t;

static esp_err_t write_table(uint8_t addr, const reg_value_t *table, size_t count) {
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = reg_write(addr, table[i].reg, table[i].value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Codec 0x%02x reg 0x%02x write failed: %s", addr, table[i].reg, esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

// Sequence proven on this board by TamaPoke: clock derived from BCLK, internal
// reference feeding the DAC, 16 kHz 16-bit I2S slave.
static esp_err_t es8311_init(void) {
    static const reg_value_t open_seq[] = {
        {0x0D, 0xFA}, {0x44, 0x08}, {0x44, 0x08},
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24},
        {0x04, 0x10}, {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00},
        {0x10, 0x1F}, {0x11, 0x7F},
        {0x00, 0x80}, {0x00, 0x80},
        {0x01, 0xBF},
    };
    static const reg_value_t clock_seq[] = {
        {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58},
        {0x02, 0x18}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x20},
    };
    static const reg_value_t start_seq[] = {
        {0x09, 0x0C}, {0x0A, 0x0C},
        {0x00, 0x80}, {0x01, 0xBF}, {0x09, 0x0C}, {0x0A, 0x0C},
        {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A},
        {0x0D, 0x01}, {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
        {0x32, 0xBF},
    };
    uint8_t value = 0;

    ESP_RETURN_ON_ERROR(write_table(ES8311_ADDR, open_seq, sizeof(open_seq) / sizeof(open_seq[0])), TAG, "ES8311 open failed");
    ESP_RETURN_ON_ERROR(reg_read(ES8311_ADDR, 0x06, &value), TAG, "ES8311 read failed");
    ESP_RETURN_ON_ERROR(reg_write(ES8311_ADDR, 0x06, value & ~0x20), TAG, "ES8311 write failed");

    ESP_RETURN_ON_ERROR(write_table(ES8311_ADDR, clock_seq, sizeof(clock_seq) / sizeof(clock_seq[0])), TAG, "ES8311 clock failed");
    ESP_RETURN_ON_ERROR(reg_read(ES8311_ADDR, 0x07, &value), TAG, "ES8311 read failed");
    ESP_RETURN_ON_ERROR(reg_write(ES8311_ADDR, 0x07, value & 0xC0), TAG, "ES8311 write failed");
    ESP_RETURN_ON_ERROR(reg_write(ES8311_ADDR, 0x08, 0xFF), TAG, "ES8311 write failed");
    ESP_RETURN_ON_ERROR(reg_read(ES8311_ADDR, 0x06, &value), TAG, "ES8311 read failed");
    ESP_RETURN_ON_ERROR(reg_write(ES8311_ADDR, 0x06, (value & 0xE0) | 0x03), TAG, "ES8311 write failed");

    ESP_RETURN_ON_ERROR(write_table(ES8311_ADDR, start_seq, sizeof(start_seq) / sizeof(start_seq[0])), TAG, "ES8311 start failed");
    ESP_RETURN_ON_ERROR(reg_read(ES8311_ADDR, 0x31, &value), TAG, "ES8311 read failed");
    return reg_write(ES8311_ADDR, 0x31, value & 0x9F);  // Unmute
}

// Register values follow Espressif's esp_codec_dev ES7210 driver: I2S slave,
// 16-bit, MCLK 4.096 MHz (256 x 16 kHz), microphones 1 and 2 at 30 dB.
static esp_err_t es7210_init(void) {
    static const reg_value_t seq[] = {
        {0x00, 0xFF}, {0x00, 0x32},                 // Reset
        {0x01, 0x3F},                               // Clocks off during setup
        {0x09, 0x30}, {0x0A, 0x30},                 // Power-up timing
        {0x23, 0x2A}, {0x22, 0x0A},                 // High-pass filter, ADC 1-2
        {0x20, 0x0A}, {0x21, 0x2A},                 // High-pass filter, ADC 3-4
        {0x08, 0x10},                               // I2S slave
        {0x40, 0x43},                               // Analog reference
        {0x41, 0x70}, {0x42, 0x70},                 // Microphone bias
        {0x07, 0x20},                               // Oversampling
        {0x02, 0xC1},                               // Main clock divider
        {0x11, 0x60}, {0x12, 0x00},                 // I2S format, 16-bit, no TDM
        {0x43, 0x1A}, {0x44, 0x1A},                 // MIC1, MIC2 on at 30 dB
        {0x45, 0x00}, {0x46, 0x00},                 // MIC3, MIC4 off
        {0x47, 0x08}, {0x48, 0x08}, {0x49, 0x08}, {0x4A, 0x08},
        {0x4B, 0x00}, {0x4C, 0xFF},                 // Power MIC1-2 on, MIC3-4 off
        {0x01, 0x00},                               // Clocks on
        {0x06, 0x00},                               // Power up
        {0x00, 0x71}, {0x00, 0x41},                 // Restart state machine
    };
    return write_table(ES7210_ADDR, seq, sizeof(seq) / sizeof(seq[0]));
}

esp_err_t audio_init(AudioDirection direction) {
    ESP_LOGI(TAG, "Initializing audio (direction=%d)...", direction);

    bool use_rx = direction == AUDIO_DIR_IN || direction == AUDIO_DIR_DUPLEX;
    bool use_tx = direction == AUDIO_DIR_OUT || direction == AUDIO_DIR_DUPLEX;

    gpio_config_t amp_config = {
        .pin_bit_mask = 1ULL << PIN_SPEAKER_AMP,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&amp_config), TAG, "Amp pin config failed");
    gpio_set_level(PIN_SPEAKER_AMP, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, use_tx ? &tx_handle : NULL, use_rx ? &rx_handle : NULL),
                        TAG, "I2S channel create failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_PIN_MCLK,
            .bclk = I2S_PIN_BCLK,
            .ws = I2S_PIN_WS,
            .dout = use_tx ? I2S_PIN_DOUT : I2S_GPIO_UNUSED,
            .din = use_rx ? I2S_PIN_DIN : I2S_GPIO_UNUSED,
        },
    };

    // Start the clocks first: both codecs are I2S slaves and need them running.
    if (use_rx) {
        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &std_cfg), TAG, "I2S RX init failed");
    }
    if (use_tx) {
        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg), TAG, "I2S TX init failed");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "I2S TX enable failed");
    }
    if (use_rx) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "I2S RX enable failed");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    if (use_tx) {
        ESP_RETURN_ON_ERROR(es8311_init(), TAG, "Speaker codec (ES8311) not responding");
        ESP_LOGI(TAG, "✓ Speaker codec ready");
    }
    if (use_rx) {
        ESP_RETURN_ON_ERROR(es7210_init(), TAG, "Microphone codec (ES7210) not responding");
        ESP_LOGI(TAG, "✓ Microphone codec ready");
    }

    ESP_LOGI(TAG, "✓ Audio driver ready (16kHz, 16-bit, mono)");
    return ESP_OK;
}

// The I2S channels stay running because they clock both codecs.
esp_err_t audio_start_recording(void) {
    return rx_handle ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t audio_stop_recording(void) {
    return rx_handle ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t audio_read_samples(int16_t *buffer, size_t buffer_size, size_t *read_size) {
    if (!rx_handle || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t stereo[CHUNK_FRAMES * 2];
    size_t wanted = buffer_size / sizeof(int16_t);
    size_t done = 0;

    while (done < wanted) {
        size_t frames = wanted - done;
        if (frames > CHUNK_FRAMES) frames = CHUNK_FRAMES;

        size_t got = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, stereo, frames * 2 * sizeof(int16_t), &got, pdMS_TO_TICKS(200));
        if (ret != ESP_OK) {
            if (read_size) *read_size = done * sizeof(int16_t);
            return ret;
        }

        size_t got_frames = got / (2 * sizeof(int16_t));
        for (size_t i = 0; i < got_frames; i++) {
            buffer[done + i] = (int16_t)(((int32_t)stereo[i * 2] + stereo[i * 2 + 1]) / 2);
        }
        done += got_frames;
    }

    if (read_size) *read_size = done * sizeof(int16_t);
    return ESP_OK;
}

esp_err_t audio_write_samples(const int16_t *buffer, size_t buffer_size) {
    if (!tx_handle || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t stereo[CHUNK_FRAMES * 2];
    size_t total = buffer_size / sizeof(int16_t);
    size_t done = 0;

    while (done < total) {
        size_t frames = total - done;
        if (frames > CHUNK_FRAMES) frames = CHUNK_FRAMES;
        for (size_t i = 0; i < frames; i++) {
            stereo[i * 2] = buffer[done + i];
            stereo[i * 2 + 1] = buffer[done + i];
        }

        size_t written = 0;
        esp_err_t ret = i2s_channel_write(tx_handle, stereo, frames * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(500));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write audio: %s", esp_err_to_name(ret));
            return ret;
        }
        done += written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

void audio_set_speaker(bool enabled) {
    gpio_set_level(PIN_SPEAKER_AMP, enabled ? 1 : 0);
}

esp_err_t audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;

    // ES8311 DAC volume: 0x00 is -95.5 dB and 0xBF is 0 dB, in 0.5 dB steps.
    esp_err_t ret = reg_write(ES8311_ADDR, 0x32, (volume * 0xBF) / 100);
    if (ret == ESP_OK) {
        current_volume = volume;
    }
    return ret;
}

uint8_t audio_get_volume(void) {
    return current_volume;
}

esp_err_t audio_deinit(void) {
    audio_set_speaker(false);

    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }

    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }

    ESP_LOGI(TAG, "Audio driver deinitialized");
    return ESP_OK;
}

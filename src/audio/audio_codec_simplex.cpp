#include "audio_codec_simplex.h"
#include "esp_log.h"
#include "esp_check.h"
#include <algorithm>
#include <cmath>
#include <climits>

static const char *TAG = "AudioCodecSimplex";

AudioCryCodecSimplex::AudioCryCodecSimplex() {}

AudioCryCodecSimplex::~AudioCryCodecSimplex()
{
    i2s_driver_uninstall(I2S_TX_PORT);
    i2s_driver_uninstall(I2S_RX_PORT);
    if (_nvs_opened)
    {
        nvs_close(_nvs_handle);
    }
}

esp_err_t AudioCryCodecSimplex::init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_open(NVS_NAMESPACE_AUDIO, NVS_READWRITE, &_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    _nvs_opened = true;

    uint8_t vol = 80;
    if (nvs_get_u8(_nvs_handle, NVS_KEY_OUTPUT_VOLUME, &vol) == ESP_OK)
    {
        _volume_percent = vol;
    }
    else
    {
        _volume_percent = 80;
        nvs_set_u8(_nvs_handle, NVS_KEY_OUTPUT_VOLUME, _volume_percent);
        nvs_commit(_nvs_handle);
    }
    update_volume_factor();
    ESP_LOGI(TAG, "Volume loaded: %u", _volume_percent);
    return ESP_OK;
}

void AudioCryCodecSimplex::update_volume_factor()
{
    float v = static_cast<float>(_volume_percent) / 100.0f;
    _volume_factor = v * v;
}

esp_err_t AudioCryCodecSimplex::Init()
{
    ESP_LOGI(TAG, "Init AudioCryCodecSimplex");
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init");

    // TX (speaker) legacy I2S
    i2s_config_t tx_config = {};
    tx_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    tx_config.sample_rate = I2S_TX_SAMPLE_RATE;
    tx_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    tx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    tx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    tx_config.dma_buf_count = 6;
    tx_config.dma_buf_len = 240;
    tx_config.use_apll = false;
    tx_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    tx_config.tx_desc_auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_driver_install(I2S_TX_PORT, &tx_config, 0, nullptr), TAG, "tx install");

    i2s_pin_config_t tx_pins = {};
    tx_pins.bck_io_num = I2S_TX_BCLK_GPIO;
    tx_pins.ws_io_num = I2S_TX_LRCK_GPIO;
    tx_pins.data_out_num = I2S_TX_DOUT_GPIO;
    tx_pins.data_in_num = I2S_PIN_NO_CHANGE;
    ESP_RETURN_ON_ERROR(i2s_set_pin(I2S_TX_PORT, &tx_pins), TAG, "tx pins");

    // RX (mic) legacy I2S
    i2s_config_t rx_config = {};
    rx_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    rx_config.sample_rate = I2S_RX_SAMPLE_RATE;
    rx_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    rx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    rx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    rx_config.dma_buf_count = 6;
    rx_config.dma_buf_len = 240;
    rx_config.use_apll = false;
    rx_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_RETURN_ON_ERROR(i2s_driver_install(I2S_RX_PORT, &rx_config, 0, nullptr), TAG, "rx install");

    i2s_pin_config_t rx_pins = {};
    rx_pins.bck_io_num = I2S_RX_BCLK_GPIO;
    rx_pins.ws_io_num = I2S_RX_LRCK_GPIO;
    rx_pins.data_in_num = I2S_RX_DIN_GPIO;
    rx_pins.data_out_num = I2S_PIN_NO_CHANGE;
    ESP_RETURN_ON_ERROR(i2s_set_pin(I2S_RX_PORT, &rx_pins), TAG, "rx pins");

    ESP_LOGI(TAG, "Init done");
    return ESP_OK;
}

esp_err_t AudioCryCodecSimplex::Start()
{
    ESP_RETURN_ON_ERROR(i2s_start(I2S_TX_PORT), TAG, "tx start");
    ESP_RETURN_ON_ERROR(i2s_start(I2S_RX_PORT), TAG, "rx start");
    ESP_LOGI(TAG, "AudioCryCodecSimplex started");
    return ESP_OK;
}

esp_err_t AudioCryCodecSimplex::EnableInput(bool enable)
{
    _input_enabled = enable;
    ESP_LOGI(TAG, "Input %s", enable ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

esp_err_t AudioCryCodecSimplex::EnableOutput(bool enable)
{
    _output_enabled = enable;
    ESP_LOGI(TAG, "Output %s", enable ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

size_t AudioCryCodecSimplex::InputData(int16_t *buffer, size_t samples, uint32_t timeout_ms)
{
    if (!_input_enabled)
        return 0;
    size_t bytes_read = 0;
    esp_err_t err = i2s_read(I2S_RX_PORT, buffer, samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
        ESP_LOGE(TAG, "i2s read: %s", esp_err_to_name(err));
        return 0;
    }
    return bytes_read / sizeof(int16_t);
}

size_t AudioCryCodecSimplex::OutputData(const int16_t *buffer, size_t samples, uint32_t timeout_ms)
{
    if (!_output_enabled)
        return 0;
    static constexpr size_t MAX_CHUNK = 256;
    size_t total_written = 0;
    while (total_written < samples)
    {
        size_t chunk = std::min(MAX_CHUNK, samples - total_written);
        int16_t tmp16[MAX_CHUNK];
        for (size_t i = 0; i < chunk; ++i)
        {
            float scaled = static_cast<float>(buffer[total_written + i]) * _volume_factor;
            if (scaled > 32767.0f)
                scaled = 32767.0f;
            if (scaled < -32768.0f)
                scaled = -32768.0f;
            tmp16[i] = static_cast<int16_t>(scaled);
        }
        size_t bytes_written = 0;
        esp_err_t err = i2s_write(I2S_TX_PORT, tmp16, chunk * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(timeout_ms));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "i2s write: %s", esp_err_to_name(err));
            break;
        }
        total_written += bytes_written / sizeof(int16_t);
        if (bytes_written == 0)
            break;
    }
    return total_written;
}

esp_err_t AudioCryCodecSimplex::SetOutputVolume(uint8_t volume_percent)
{
    _volume_percent = std::min<uint8_t>(100, volume_percent);
    update_volume_factor();
    ESP_LOGI(TAG, "Set volume: %u (factor=%.3f)", _volume_percent, _volume_factor);
    if (_nvs_opened)
    {
        nvs_set_u8(_nvs_handle, NVS_KEY_OUTPUT_VOLUME, _volume_percent);
        nvs_commit(_nvs_handle);
    }
    return ESP_OK;
}

esp_err_t AudioCryCodecSimplex::SetInputGain(int gain_db)
{
    _input_gain_db = gain_db;
    ESP_LOGI(TAG, "Set input gain (placeholder): %d dB", gain_db);
    return ESP_OK;
}

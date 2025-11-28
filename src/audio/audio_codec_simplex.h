#pragma once

#include "audio_codec.h"
#include "driver/i2s.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "board_config.h"

class AudioCryCodecSimplex : public AudioCodec
{
public:
    AudioCryCodecSimplex();
    ~AudioCryCodecSimplex() override;

    esp_err_t Init() override;
    esp_err_t Start() override;

    esp_err_t EnableInput(bool enable) override;
    esp_err_t EnableOutput(bool enable) override;

    size_t InputData(int16_t *buffer, size_t samples, uint32_t timeout_ms) override;
    size_t OutputData(const int16_t *buffer, size_t samples, uint32_t timeout_ms) override;

    esp_err_t SetOutputVolume(uint8_t volume_percent) override;
    esp_err_t SetInputGain(int gain_db) override;

private:
    bool _input_enabled = false;
    bool _output_enabled = false;

    uint8_t _volume_percent = 80;   // default
    float _volume_factor = 0.64f;   // 0.8^2
    int _input_gain_db = 0;

    nvs_handle_t _nvs_handle = 0;
    bool _nvs_opened = false;

    esp_err_t init_nvs();
    void update_volume_factor();
};

#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

class AudioCodec
{
public:
    virtual ~AudioCodec() = default;

    virtual esp_err_t Init() = 0;
    virtual esp_err_t Start() = 0;

    virtual esp_err_t EnableInput(bool enable) = 0;
    virtual esp_err_t EnableOutput(bool enable) = 0;

    // Read 16-bit mono samples from mic
    virtual size_t InputData(int16_t *buffer, size_t samples, uint32_t timeout_ms) = 0;

    // Write 16-bit mono samples to speaker (volume scale inside codec)
    virtual size_t OutputData(const int16_t *buffer, size_t samples, uint32_t timeout_ms) = 0;

    // Volume 0–100, squared scaling
    virtual esp_err_t SetOutputVolume(uint8_t volume_percent) = 0;

    // Placeholder gain setter
    virtual esp_err_t SetInputGain(int gain_db) = 0;
};

#pragma once
#include <Arduino.h>
#include <FS.h>

struct IMAState {
    int16_t predictor;
    int8_t index;
};

// Decode một block IMA ADPCM 4-bit (WAV IMA chuẩn) vào PCM 16-bit.
// adpcm_len_bytes >= 4 (header predictor/index + nibbles)
bool decodeIMA4BitBlock(const uint8_t* adpcm, size_t adpcm_len_bytes,
                        int16_t* pcm_out, size_t& pcm_samples, IMAState& st);

// Thông tin WAV IMA
struct WAVInfo {
    uint32_t sampleRate = 16000;
    uint16_t numChannels = 1;
    uint16_t blockAlign = 0;
    bool ok = false;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
};

// Đọc header WAV IMA ADPCM (fmt=0x0011)
WAVInfo parseWavIMA(File& f);

#pragma once

#include <cstddef>
#include <cstdint>

// Tiny footprint features for no-PSRAM: MFCC-like 20 x 25
constexpr int kMelBins = 20;      // output coefficients (treated as MFCC)
constexpr int kMelFrames = 25;    // ~2 s with 80 ms hop
constexpr int kFftSize = 512;
constexpr int kFrameLength = 512;
constexpr int kFrameHop = 1280;   // 80 ms @16 kHz
constexpr int kSampleRate = 16000;
constexpr int kRequiredSamples = kFrameHop * (kMelFrames - 1) + kFrameLength;

// Compute log-mel/MFCC features (kMelBins x kMelFrames) from PCM buffer.
// Output is written frame-major (frame outer, mel inner) to `mel_out`.
bool ComputeLogMelSpectrogram(const int16_t* pcm, size_t num_samples, float* mel_out);

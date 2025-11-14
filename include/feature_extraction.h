#pragma once

#include <cstddef>
#include <cstdint>

constexpr int kMelBins = 64;
constexpr int kMelFrames = 128;
constexpr int kFftSize = 512;
constexpr int kFrameLength = 400;
constexpr int kFrameHop = 160;
constexpr int kSampleRate = 16000;
constexpr int kRequiredSamples = kFrameHop * (kMelFrames - 1) + kFrameLength;

// Compute log-mel spectrogram (64 x 128) from PCM buffer.
// `pcm` must contain at least kRequiredSamples samples (will zero-pad if longer).
// Output is written in mel-major order (mel index fastest) to `mel_out`.
bool ComputeLogMelSpectrogram(const int16_t* pcm, size_t num_samples, float* mel_out);

// Apply per-mel standardisation using training mean/std.
void StandardizeMelBands(float* mel_features);

#include "feature_extraction.h"

#include <array>
#include <cmath>
#include <cstring>

#include "kiss_fftr.h"

namespace {

constexpr float kLogEps = 1e-6f;

std::array<float, kFrameLength> MakeHannWindow() {
  std::array<float, kFrameLength> window{};
  for (int i = 0; i < kFrameLength; ++i) {
    window[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (kFrameLength - 1));
  }
  return window;
}

float HzToMel(float hz) {
  return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MelToHz(float mel) {
  return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

std::array<float, kMelBins * (kFftSize / 2 + 1)> BuildMelFilterbank() {
  constexpr int kNumFftBins = kFftSize / 2 + 1;
  std::array<float, kMelBins * kNumFftBins> filters{};
  const float mel_low = HzToMel(20.0f);
  const float mel_high = HzToMel(kSampleRate / 2.0f);

  std::array<float, kMelBins + 2> mel_points{};
  for (int i = 0; i < kMelBins + 2; ++i) {
    mel_points[i] = mel_low + (mel_high - mel_low) * i / (kMelBins + 1);
  }

  std::array<float, kMelBins + 2> hz_points{};
  for (int i = 0; i < kMelBins + 2; ++i) {
    hz_points[i] = MelToHz(mel_points[i]);
  }

  std::array<int, kMelBins + 2> bin_points{};
  for (int i = 0; i < kMelBins + 2; ++i) {
    bin_points[i] = static_cast<int>(std::floor((kFftSize + 1) * hz_points[i] / kSampleRate));
  }

  for (int m = 1; m <= kMelBins; ++m) {
    int start = bin_points[m - 1];
    int center = bin_points[m];
    int end = bin_points[m + 1];
    for (int k = start; k < center; ++k) {
      if (k >= 0 && k < kNumFftBins) {
        filters[(m - 1) * kNumFftBins + k] = (k - start) / static_cast<float>(center - start + 1);
      }
    }
    for (int k = center; k < end; ++k) {
      if (k >= 0 && k < kNumFftBins) {
        filters[(m - 1) * kNumFftBins + k] = (end - k) / static_cast<float>(end - center + 1);
      }
    }
  }

  return filters;
}

const std::array<float, kFrameLength> kHannWindow = MakeHannWindow();
const std::array<float, kMelBins * (kFftSize / 2 + 1)> kMelFilters = BuildMelFilterbank();

}  // namespace

bool ComputeLogMelSpectrogram(const int16_t* pcm, size_t num_samples, float* mel_out) {
  if (mel_out == nullptr) {
    return false;
  }

  const int num_fft_bins = kFftSize / 2 + 1;

  static kiss_fftr_cfg cfg = nullptr;
  if (!cfg) {
    cfg = kiss_fftr_alloc(kFftSize, 0, nullptr, nullptr);
    if (!cfg) {
      return false;
    }
  }

  std::array<float, kFftSize> frame_buffer{};
  std::array<float, num_fft_bins> power_spectrum{};

  for (int frame = 0; frame < kMelFrames; ++frame) {
    const int offset = frame * kFrameHop;
    // Prepare windowed frame
    for (int i = 0; i < kFrameLength; ++i) {
      int idx = offset + i;
      float sample = 0.0f;
      if (idx < static_cast<int>(num_samples)) {
        sample = static_cast<float>(pcm[idx]) / 32768.0f;
      }
      frame_buffer[i] = sample * kHannWindow[i];
    }
    for (int i = kFrameLength; i < kFftSize; ++i) {
      frame_buffer[i] = 0.0f;
    }

    // FFT
    std::array<kiss_fft_cpx, num_fft_bins> fft_out{};
    kiss_fftr(cfg, frame_buffer.data(), fft_out.data());

    for (int i = 0; i < num_fft_bins; ++i) {
      const float re = fft_out[i].r;
      const float im = fft_out[i].i;
      power_spectrum[i] = re * re + im * im;
    }

    // Apply mel filterbank (frame-major layout: frame outer, mel inner)
    for (int m = 0; m < kMelBins; ++m) {
      const float* filter = &kMelFilters[m * num_fft_bins];
      float mel_energy = 0.0f;
      for (int i = 0; i < num_fft_bins; ++i) {
        mel_energy += filter[i] * power_spectrum[i];
      }
      mel_energy = std::log(std::max(mel_energy, kLogEps));
      const int idx = frame * kMelBins + m;
      mel_out[idx] = mel_energy;
    }
  }

  // Global mean/std normalize (per full 20x25 window)
  float mean = 0.0f;
  const int total = kMelBins * kMelFrames;
  for (int i = 0; i < total; ++i) mean += mel_out[i];
  mean /= static_cast<float>(total);
  float var = 0.0f;
  for (int i = 0; i < total; ++i) {
    float d = mel_out[i] - mean;
    var += d * d;
  }
  var /= static_cast<float>(total);
  float std = std::sqrt(std::max(var, kLogEps));
  for (int i = 0; i < total; ++i) {
    mel_out[i] = (mel_out[i] - mean) / std;
  }

  return true;
}

// No-op: per-window normalization is already applied
void StandardizeMelBands(float* /*mel_features*/) {}

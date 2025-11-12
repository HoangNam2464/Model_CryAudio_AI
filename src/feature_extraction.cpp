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
  const float mel_low = HzToMel(0.0f);
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

const std::array<float, kMelBins> kMelMean = {
    0.096575916f, 0.213134080f, 0.140941516f, 0.219726443f, 0.171807364f, 0.312976509f,
    0.335771203f, 0.461974174f, 0.441300780f, 0.462244242f, 0.402601004f, 0.399011105f,
    0.371090919f, 0.407867700f, 0.428931475f, 0.466444492f, 0.479145825f, 0.474110842f,
    0.476285309f, 0.444385052f, 0.438051671f, 0.393129021f, 0.352931112f, 0.348689407f,
    0.293612033f, 0.285360992f, 0.272340894f, 0.257353574f, 0.232156426f, 0.241718903f,
    0.216362521f, 0.206581697f, 0.172105521f, 0.161687672f, 0.152412146f, 0.152177364f,
    0.149543688f, 0.132690787f, 0.124033310f, 0.093045078f, 0.074630715f, 0.046879392f,
   -0.007031537f,-0.054310393f,-0.071843036f,-0.103770919f,-0.147775933f,-0.211251289f,
   -0.275959909f,-0.395474494f,-0.558277488f,-0.590667725f,-0.628493249f,-0.651770949f,
   -0.673963189f,-0.710214257f,-0.736975074f,-0.746896386f,-0.771948636f,-0.807865024f,
   -0.844050467f,-0.899151921f,-0.977511287f,-1.138618469f};

const std::array<float, kMelBins> kMelStd = {
    1.092339635f, 1.067350984f, 1.000815988f, 0.917115629f, 0.884375870f, 0.928738236f,
    0.986661732f, 1.022876620f, 1.018283606f, 0.980814338f, 0.956476271f, 0.939813256f,
    0.947132051f, 0.961208045f, 0.976889610f, 0.988389969f, 0.990566492f, 0.987024546f,
    0.986420870f, 0.976130784f, 0.961527944f, 0.955800235f, 0.954691231f, 0.950352192f,
    0.932095647f, 0.916217029f, 0.908481896f, 0.903618813f, 0.900158107f, 0.899630129f,
    0.889528334f, 0.872039616f, 0.851082325f, 0.841885507f, 0.844096482f, 0.852624834f,
    0.854118943f, 0.859634042f, 0.865779758f, 0.869614303f, 0.877070487f, 0.886339664f,
    0.869833469f, 0.858061969f, 0.849586546f, 0.840107620f, 0.829616964f, 0.822271347f,
    0.811051786f, 0.792517066f, 0.806069493f, 0.805630326f, 0.802735448f, 0.810989022f,
    0.804208398f, 0.792219281f, 0.781385958f, 0.777333617f, 0.778067470f, 0.758202016f,
    0.751913786f, 0.740969658f, 0.772831738f, 0.745054305f};

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

    // Apply mel filterbank
    for (int m = 0; m < kMelBins; ++m) {
      const float* filter = &kMelFilters[m * num_fft_bins];
      float mel_energy = 0.0f;
      for (int i = 0; i < num_fft_bins; ++i) {
        mel_energy += filter[i] * power_spectrum[i];
      }
      mel_energy = std::log(std::max(mel_energy, kLogEps));
      mel_out[m * kMelFrames + frame] = mel_energy;
    }
  }

  return true;
}

void StandardizeMelBands(float* mel_features) {
  if (!mel_features) {
    return;
  }
  for (int m = 0; m < kMelBins; ++m) {
    const float mean = kMelMean[m];
    const float std = kMelStd[m];
    const float inv_std = 1.0f / std;
    for (int t = 0; t < kMelFrames; ++t) {
      const int idx = m * kMelFrames + t;
      mel_features[idx] = (mel_features[idx] - mean) * inv_std;
    }
  }
}

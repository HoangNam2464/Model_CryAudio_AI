#include "tflm_infer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "crynet_model.h"
#include "feature_extraction.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr size_t kTensorArenaSize = 180 * 1024;  // adjust if AllocateTensors fails
constexpr size_t kFeatureCount = kMelBins * kMelFrames;

alignas(16) uint8_t g_tensor_arena[kTensorArenaSize];
const tflite::Model* g_model = nullptr;
tflite::AllOpsResolver g_resolver;
tflite::MicroInterpreter* g_interpreter = nullptr;
TfLiteTensor* g_input = nullptr;
TfLiteTensor* g_output = nullptr;
bool g_ready = false;
std::array<float, kFeatureCount> g_mel_buffer{};

bool CopyFeaturesToInput() {
    if (!g_input) {
        return false;
    }
    switch (g_input->type) {
        case kTfLiteFloat32: {
            const size_t bytes_needed = kFeatureCount * sizeof(float);
            if (g_input->bytes < bytes_needed) {
                return false;
            }
            std::memcpy(g_input->data.f, g_mel_buffer.data(), bytes_needed);
            return true;
        }
        case kTfLiteInt8: {
            if (g_input->bytes < kFeatureCount) {
                return false;
            }
            if (g_input->params.scale == 0.0f) {
                return false;
            }
            for (size_t i = 0; i < kFeatureCount; ++i) {
                const float scaled = g_mel_buffer[i] / g_input->params.scale +
                                     static_cast<float>(g_input->params.zero_point);
                int32_t q = static_cast<int32_t>(std::lround(scaled));
                q = std::max(-128, std::min(127, q));
                g_input->data.int8[i] = static_cast<int8_t>(q);
            }
            return true;
        }
        default:
            return false;
    }
}

int OutputElementCount(const TfLiteTensor* tensor) {
    if (!tensor || !tensor->dims) {
        return 0;
    }
    int count = 1;
    for (int i = 0; i < tensor->dims->size; ++i) {
        count *= tensor->dims->data[i];
    }
    return count;
}

float ReadOutputProb() {
    if (!g_output) {
        return 0.0f;
    }

    const int count = OutputElementCount(g_output);
    const int idx = (count >= 2) ? (count - 1) : 0;  // assume last logit = "cry"
    float prob = 0.0f;

    switch (g_output->type) {
        case kTfLiteFloat32:
            prob = g_output->data.f[idx];
            break;
        case kTfLiteInt8: {
            const int32_t raw = static_cast<int32_t>(g_output->data.int8[idx]);
            prob = (raw - g_output->params.zero_point) * g_output->params.scale;
            break;
        }
        case kTfLiteUInt8: {
            const int32_t raw = static_cast<int32_t>(g_output->data.uint8[idx]);
            prob = (raw - g_output->params.zero_point) * g_output->params.scale;
            break;
        }
        default:
            return 0.0f;
    }

    return std::clamp(prob, 0.0f, 1.0f);
}

}  // namespace

bool tflm_begin() {
    g_model = tflite::GetModel(crynet_int8_tflite);
    if (!g_model || g_model->version() != TFLITE_SCHEMA_VERSION) {
        return false;
    }

    static tflite::MicroInterpreter s_interpreter(
        g_model, g_resolver, g_tensor_arena, kTensorArenaSize);

    if (s_interpreter.AllocateTensors() != kTfLiteOk) {
        return false;
    }

    g_interpreter = &s_interpreter;
    g_input = g_interpreter->input(0);
    g_output = g_interpreter->output(0);
    g_ready = (g_input != nullptr && g_output != nullptr);
    return g_ready;
}

float tflm_infer_prob(const int16_t* pcm, size_t n_samples) {
    if (!g_ready || !pcm) {
        return 0.0f;
    }

    if (!ComputeLogMelSpectrogram(pcm, n_samples, g_mel_buffer.data())) {
        return 0.0f;
    }
    StandardizeMelBands(g_mel_buffer.data());

    if (!CopyFeaturesToInput()) {
        return 0.0f;
    }

    if (g_interpreter->Invoke() != kTfLiteOk) {
        return 0.0f;
    }

    return ReadOutputProb();
}

#include "tflm_infer.h"
#include <Arduino.h>
#include <HardwareSerial.h>
extern HardwareSerial Serial0;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <new>
#include <cstdint>
#include <esp_attr.h>

#include "crynet_tiny_no_psram.h"

// Nhúng trực tiếp model (đã loại khỏi src_filter nên không build thành .o riêng)
#include "../../src/model/crynet_tiny_no_psram.cc"
#include "feature_extraction.h"
#include "esp_heap_caps.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/kernels/reduce.cc"  // Register_MEAN
#include "tensorflow/lite/micro/kernels/reduce_common.cc"

namespace {

// Arena nhỏ cho model tiny (no-PSRAM)
constexpr size_t kTensorArenaSize = 28 * 1024;  // 28 KB
constexpr size_t kFeatureCount = kMelBins * kMelFrames;

uint8_t* g_tensor_arena = nullptr;
const tflite::Model* g_model = nullptr;
tflite::MicroMutableOpResolver<12> g_resolver;
tflite::MicroInterpreter* g_interpreter = nullptr;
TfLiteTensor* g_input = nullptr;
TfLiteTensor* g_output = nullptr;
bool g_ready = false;
float* g_mel_buffer = nullptr;

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
            std::memcpy(g_input->data.f, g_mel_buffer, bytes_needed);
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
    // Mapping: output[0]=NOT_CRY, output[1]=CRY (model DS-CNN tiny). Fallback idx=0 if only 1 class.
    const int idx = (count >= 2) ? 1 : 0;
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
    g_model = tflite::GetModel(crynet_tiny_no_psram);
    if (!g_model || g_model->version() != TFLITE_SCHEMA_VERSION) {
        return false;
    }
    size_t free_int_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_ps_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial0.printf("[AI] heap before init int=%u psram=%u\n", (unsigned)free_int_before, (unsigned)free_ps_before);

    static bool resolver_init = false;
    if (!resolver_init) {
        if (g_resolver.AddConv2D() != kTfLiteOk) return false;
        if (g_resolver.AddDepthwiseConv2D() != kTfLiteOk) return false;
        if (g_resolver.AddFullyConnected() != kTfLiteOk) return false;
        if (g_resolver.AddSoftmax() != kTfLiteOk) return false;
        if (g_resolver.AddReshape() != kTfLiteOk) return false;
        if (g_resolver.AddMean() != kTfLiteOk) return false;
        resolver_init = true;
    }

    // Kiểm tra nhanh dung lượng heap trước khi cấp phát lớn
    const size_t mel_bytes = kFeatureCount * sizeof(float);
    const size_t safety_margin = 8 * 1024;  // 8 KB
    const size_t need_bytes = kTensorArenaSize + mel_bytes + safety_margin;
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t free_total = free_internal + free_psram;
    if (free_total < need_bytes) {
        Serial0.printf("[AI] Not enough heap for TFLM (int=%u, psram=%u, need=%u). Use S3 PSRAM or reduce arena.\n",
                       (unsigned)free_internal, (unsigned)free_psram, (unsigned)need_bytes);
        return false;
    }
    if (!g_tensor_arena) {
        g_tensor_arena = static_cast<uint8_t*>(
            heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!g_tensor_arena) {
            g_tensor_arena = static_cast<uint8_t*>(
                heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (!g_tensor_arena) {
            Serial0.printf("[AI] Failed to alloc tensor arena (%u bytes, free=%u)\n",
                          (unsigned)kTensorArenaSize, (unsigned)esp_get_free_heap_size());
            return false;
        }
    }

    if (!g_mel_buffer) {
        g_mel_buffer = static_cast<float*>(
            heap_caps_malloc(kFeatureCount * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!g_mel_buffer) {
            g_mel_buffer = static_cast<float*>(
                heap_caps_malloc(kFeatureCount * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (!g_mel_buffer) {
            Serial0.printf("[AI] Failed to alloc mel buffer (%u bytes, free=%u)\n",
                          (unsigned)(kFeatureCount * sizeof(float)), (unsigned)esp_get_free_heap_size());
            return false;
        }
    }

    static tflite::MicroInterpreter* s_interpreter = nullptr;
    static uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));
    if (!s_interpreter) {
        s_interpreter = new(interpreter_storage)
            tflite::MicroInterpreter(g_model, g_resolver, g_tensor_arena, kTensorArenaSize);
    }

    if (s_interpreter->AllocateTensors() != kTfLiteOk) {
        Serial0.println("[AI] AllocateTensors failed");
        return false;
    }

    g_interpreter = s_interpreter;
    g_input = g_interpreter->input(0);
    g_output = g_interpreter->output(0);
    g_ready = (g_input != nullptr && g_output != nullptr);
    Serial0.printf("[AI] TFLM ready. arena_used=%u bytes, input scale=%.6f zp=%d\n",
                   (unsigned)g_interpreter->arena_used_bytes(),
                   g_input ? g_input->params.scale : 0.0f,
                   g_input ? g_input->params.zero_point : 0);
    Serial0.printf("[AI] output scale=%.6f zp=%d\n",
                   g_output ? g_output->params.scale : 0.0f,
                   g_output ? g_output->params.zero_point : 0);
    size_t free_int_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_ps_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial0.printf("[AI] heap after init int=%u psram=%u\n", (unsigned)free_int_after, (unsigned)free_ps_after);
    return g_ready;
}

float tflm_infer_prob(const int16_t* pcm, size_t n_samples) {
    if (!g_ready || !pcm) {
        return 0.0f;
    }

    if (!ComputeLogMelSpectrogram(pcm, n_samples, g_mel_buffer)) {
        return 0.0f;
    }

    if (!CopyFeaturesToInput()) {
        return 0.0f;
    }

    if (g_interpreter->Invoke() != kTfLiteOk) {
        return 0.0f;
    }

    return ReadOutputProb();
}

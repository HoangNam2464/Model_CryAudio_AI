#pragma once
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// DS-CNN quantized TFLite model (binary embedded array)
extern const unsigned char ds_cnn_model[];
extern const std::size_t ds_cnn_model_len;

#ifdef __cplusplus
}
#endif

#pragma once
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Model data (quantized TFLite)
extern const unsigned char crynet_int8_tflite[];
extern const std::size_t crynet_int8_tflite_len;

#ifdef __cplusplus
}
#endif

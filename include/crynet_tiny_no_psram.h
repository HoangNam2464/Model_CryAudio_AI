#pragma once
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Quantized TFLite model optimized for no-PSRAM builds
extern const unsigned char crynet_tiny_no_psram[];
extern const std::size_t crynet_tiny_no_psram_len;

#ifdef __cplusplus
}
#endif

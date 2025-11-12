#pragma once
#include <Arduino.h>
bool tflm_begin();  // init model & arena
float tflm_infer_prob(const int16_t* pcm, size_t n_samples); // return cry prob [0..1]

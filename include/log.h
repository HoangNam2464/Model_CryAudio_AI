#pragma once

#include <Arduino.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 2 // 0=ERROR,1=WARN,2=INFO,3=DEBUG
#endif

extern HardwareSerial Serial0;

#define LOG_PRINT(level, fmt, ...)            \
    do                                        \
    {                                         \
        if (LOG_LEVEL >= (level))             \
            Serial0.printf(fmt, ##__VA_ARGS__); \
    } while (0)

#define LOGE(fmt, ...) LOG_PRINT(0, "[E] " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT(1, "[W] " fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_PRINT(2, "[I] " fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_PRINT(3, "[D] " fmt, ##__VA_ARGS__)

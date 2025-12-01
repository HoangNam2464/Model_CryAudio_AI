#pragma once

#include <Arduino.h>

struct CryResult
{
    float prob = 0.0f;
    bool crying = false;
    uint32_t captured_ms = 0;
};

struct GpsReading
{
    bool valid = false;
    double lat = 0.0;
    double lng = 0.0;
    uint8_t sats = 0;

    bool has_time = false;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t age_ms = 0;
};

#pragma once

#include <Arduino.h>

#ifndef DEVICE_FW_VERSION
#define DEVICE_FW_VERSION "1.0.0"
#endif

struct CryInfo
{
    bool detected = false;
    float prob = 0.0f;
    char state[8] = "CALM";
    float threshold = 0.0f;
    uint32_t duration_ms = 0;
    uint32_t timestamp_ms = 0;
};

struct GpsInfo
{
    bool valid = false;
    double lat = 0.0;
    double lng = 0.0;
    uint8_t sats = 0;
};

struct DeviceStatus
{
    char mode[6] = "day";
    int battery_percent = 0;
    int wifi_rssi = 0;
    uint32_t uptime_ms = 0;
    char device_ip[48] = "";
    char firmware[24] = DEVICE_FW_VERSION;
};

void initBackendService();
bool sendCryEventToBackend(const CryInfo &cry, const GpsInfo &gps, const DeviceStatus &st, bool offlineBuffered = false);

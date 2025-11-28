#pragma once

#include <Arduino.h>

struct GpsSnapshot
{
    double lat = 0;
    double lng = 0;
    float speed = 0;
    uint8_t sats = 0;
    bool valid = false;
    uint32_t lastUpdateMs = 0;
};

struct WifiSnapshot
{
    bool connected = false;
    int rssi = 0;
    char ssid[33] = "";
    char ip[48] = "";
};

struct CrySnapshot
{
    float prob = 0.0f;
    float score = 0.0f;
    bool crying = false;
    uint32_t lastTs = 0;
    char lastEvent[16] = "";
};

struct AppState
{
    CrySnapshot cry;
    GpsSnapshot gps;
    WifiSnapshot wifi;
    bool nightMode = false;
    bool indoorMode = false;
    char statusMessage[64] = "Booting";
    char deviceId[32] = "";
    char firmware[24] = "";
};

void app_state_init(const char *deviceId, const char *firmware);
void app_state_set_status(const char *msg);
const char *app_state_get_status();
void app_state_set_wifi(bool connected, int rssi, const char *ssid, const char *ip);
void app_state_update_cry(float prob, float score, bool crying, uint32_t ts, const char *label);
void app_state_update_gps(double lat, double lng, float speed, uint8_t sats, bool valid, uint32_t ts);
void app_state_set_mode(bool nightMode, bool indoorMode);
String app_state_get_status_json();
const AppState &app_state_get();

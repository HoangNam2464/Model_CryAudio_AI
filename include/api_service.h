#pragma once
#include <ArduinoJson.h>

bool api_send_event(
    bool isCrying,
    float prob,
    const String &mode,
    bool gpsValid,
    double lat,
    double lng,
    int battery,
    const String &timestamp,
    int deviceDbId
);

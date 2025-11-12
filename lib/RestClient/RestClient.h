#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Config.h"

class RestClient {
public:
    static bool postJSON(const String& url, const String& json) {
        if (WiFi.status() != WL_CONNECTED) return false;
        HTTPClient http;
        if (!http.begin(url)) return false;
        http.addHeader("Content-Type", "application/json");
        if (String(API_TOKEN).length() > 0) {
            http.addHeader("Authorization", API_TOKEN);
        }
        int code = http.POST(json);
        http.end();
        return code >= 200 && code < 300;
    }
};

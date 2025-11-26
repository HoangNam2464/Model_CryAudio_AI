#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Config.h"

class RestClient {
public:
    // Trả về HTTP status code (hoặc -1 nếu lỗi kết nối/bắt đầu request)
    static int postJSON(const String& url, const String& json) {
        if (WiFi.status() != WL_CONNECTED) return -1;
        HTTPClient http;
        if (!http.begin(url)) return -1;
        http.addHeader("Content-Type", "application/json");
        if (String(API_TOKEN).length() > 0) {
            http.addHeader("Authorization", API_TOKEN);
        }
        int code = http.POST(json);
        http.end();
        return code;
    }
};

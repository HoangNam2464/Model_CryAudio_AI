#include "api_service.h"
#include "Config.h"
#include "device_id.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <FS.h>

// Fallback JSON tĩnh nếu không tìm thấy file SPIFFS.
static const char *kFallbackStaticJson =
    "{\"device_id\":\"static\",\"is_crying\":false,\"prob\":0.0,"
    "\"gps_valid\":0,\"lat\":0,\"lng\":0,\"battery\":100,\"mode\":\"STATIC\"}";

extern HardwareSerial Serial0;

// Gửi nội dung JSON tĩnh từ file thay vì dữ liệu AI động.
// File được lấy từ STATIC_JSON_PATH trong SPIFFS.
bool api_send_event(
    bool isCrying,
    float prob,
    const String &mode,
    bool gpsValid,
    double lat,
    double lng,
    int battery,
    const String &timestamp,
    int deviceDbId)
{
    (void)isCrying;
    (void)prob;
    (void)mode;
    (void)gpsValid;
    (void)lat;
    (void)lng;
    (void)battery;
    (void)timestamp;
    (void)deviceDbId;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial0.println("[API] WiFi not connected");
        return false;
    }

    String body;
    if (SPIFFS.begin(false))
    {
        File f = SPIFFS.open(STATIC_JSON_PATH, "r");
        if (!f)
        {
            Serial0.printf("[API] Khong tim thay file JSON tinh: %s (se dung fallback)\n", STATIC_JSON_PATH);
        }
        else
        {
            body = f.readString();
            f.close();
            body.trim();
            if (body.isEmpty())
            {
                Serial0.printf("[API] File JSON tinh %s rong, se dung fallback\n", STATIC_JSON_PATH);
            }
        }
    }
    else
    {
        Serial0.println("[API] SPIFFS mount failed, se dung fallback JSON");
    }

    if (body.isEmpty())
    {
        body = kFallbackStaticJson;
    }

    HTTPClient http;
    http.setTimeout(3000);
    http.begin(BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    Serial0.printf("[API] POST %s payload_len=%u (source=%s)\n",
                   BACKEND_URL, (unsigned)body.length(),
                   body == kFallbackStaticJson ? "fallback" : STATIC_JSON_PATH);

    int code = http.POST(body);
    bool ok = (code == 200 || code == 201);
    if (!ok)
    {
        String resp = http.getString();
        if (resp.length() > 160)
            resp = resp.substring(0, 160);
        Serial0.printf("[API] FAIL code=%d resp=%s\n", code, resp.c_str());
    }
    else
    {
        Serial0.printf("[API] OK code=%d\n", code);
    }

    http.end();
    return ok;
}

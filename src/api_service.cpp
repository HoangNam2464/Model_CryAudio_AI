#include "api_service.h"
#include "Config.h"
#include "device_id.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <HardwareSerial.h>

extern HardwareSerial Serial0;

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
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial0.println("[API] WiFi not connected");
        return false;
    }

    HTTPClient http;
    http.setTimeout(3000);
    http.begin(BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;

    // Ưu tiên deviceDbId nếu caller truyền >0, nếu không sẽ lấy ID từ MAC (số).
    int finalId = (deviceDbId > 0) ? deviceDbId : device_id_int();
    doc["device_id"] = finalId;
    doc["device_name"] = device_id_str();
    doc["lat"] = lat;
    doc["lng"] = lng;
    doc["is_crying"] = isCrying;
    doc["prob"] = prob;
    doc["timestamp"] = timestamp;
    doc["gps_valid"] = gpsValid ? 1 : 0;
    doc["battery"] = battery;
    doc["mode"] = mode;

    String body;
    serializeJson(doc, body);

    Serial0.printf("[API] POST %s dev=%d(%s) cry=%d prob=%.2f gps=%d lat=%.6f lng=%.6f\n",
                   BACKEND_URL, finalId, device_id_str().c_str(), isCrying, prob,
                   gpsValid ? 1 : 0, lat, lng);

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

#include "api_service.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <HardwareSerial.h>

// dùng cùng UART với toàn bộ project
extern HardwareSerial Serial0;

bool api_send_event(
    bool isCrying,
    float prob,
    const String &mode,      // hiện không dùng nữa nhưng giữ tham số cho dễ gọi
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
    http.begin(BACKEND_URL);       // VD: http://192.168.1.110:8000/api/cry-events
    http.setTimeout(4000);

    http.addHeader("Content-Type", "application/json");

    // JSON PHÙ HỢP LARAVEL (bạn đã sửa)
    DynamicJsonDocument doc(256);

    doc["device_id"] = deviceDbId;  // ID thiết bị trong DB
    doc["lat"]       = lat;
    doc["lng"]       = lng;
    doc["is_crying"] = isCrying;
    doc["prob"]      = prob;
    doc["timestamp"] = timestamp;

    String body;
    serializeJson(doc, body);

    Serial0.print("[API] POST to: ");
    Serial0.println(BACKEND_URL);
    Serial0.println("[API] Sending JSON:");
    Serial0.println(body);

    int code = http.POST(body);

    if (code == 200 || code == 201)
    {
        Serial0.printf("[API] OK sent, HTTP %d\n", code);
        http.end();
        return true;
    }

    Serial0.printf("[API] HTTP ERROR: %d\n", code);
    String resp = http.getString();
    if (resp.length())
    {
        Serial0.println("[API] Response body:");
        Serial0.println(resp);
    }
    http.end();
    return false;
}

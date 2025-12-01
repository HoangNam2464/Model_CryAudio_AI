#include "api_service.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

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
        Serial.println("[API] WiFi not connected");
        return false;
    }

    HTTPClient http;
    http.begin(BACKEND_URL);
    http.setTimeout(4000);

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + API_TOKEN);

    DynamicJsonDocument doc(256);

    doc["device_id"] = DEVICE_ID;
    doc["cry_state"] = isCrying ? "CRYING" : "CALM";
    doc["cry_prob"] = prob;
    doc["status_mode"] = mode;

    doc["gps_valid"] = gpsValid;
    doc["gps_lat"] = lat;
    doc["gps_lng"] = lng;

    doc["status_battery"] = battery;
    doc["occurred_at"] = timestamp;
    doc["id_thiet_bi"] = deviceDbId;

    String body;
    serializeJson(doc, body);

    Serial.println("[API] Sending JSON:");
    Serial.println(body);

    int code = http.POST(body);

    if (code == 200 || code == 201)
    {
        http.end();
        return true;
    }

    Serial.printf("[API] HTTP ERROR: %d\n", code);
    http.end();
    return false;
}

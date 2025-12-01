#include "backend_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>
#include <HardwareSerial.h>
#include <esp32-hal-psram.h>

#include "Config.h"
#include "wifi_service.h"
#include "log.h"
#include "AppState.h"
#include "device_id.h"

extern HardwareSerial Serial0;

static String isoTimestamp(uint32_t fallbackMs)
{
    time_t now = time(nullptr);
    if (now <= 0)
    {
        now = fallbackMs / 1000;
    }
    struct tm tmnow;
    gmtime_r(&now, &tmnow);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmnow);
    return String(buf);
}

void initBackendService()
{
    Serial0.println("[BACKEND] service init");
}

static bool do_post(const String &body)
{
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, BACKEND_URL))
    {
        Serial0.printf("[BACKEND] http.begin failed: %s\n", BACKEND_URL);
        return false;
    }
    http.setTimeout(4000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String resp = http.getString();
    http.end();
    if (resp.length() > 160)
    {
        resp = resp.substring(0, 160);
    }

    Serial0.printf("[BACKEND] POST %s => code=%d\n", BACKEND_URL, code);
    Serial0.printf("[BACKEND] body: %s\n", body.c_str());
    if (resp.length() > 0)
    {
        Serial0.printf("[BACKEND] resp: %s\n", resp.c_str());
    }

    return code >= 200 && code < 300;
}

bool sendCryEventToBackend(const CryInfo &cry, const GpsInfo &gps, const DeviceStatus &st, bool offlineBuffered)
{
    if (!wifi_ensure_connected(2000) || WiFi.status() != WL_CONNECTED)
    {
        LOGW("[BACKEND] WiFi not ready, skip POST");
        return false;
    }

    JsonDocument doc;
    doc["device_id"] = device_id_str();
    doc["device_fw_version"] = (st.firmware[0] != '\0') ? st.firmware : DEVICE_FW_VERSION;
    doc["device_ip"] = st.device_ip;
    doc["esp32_chip_model"] = ESP.getChipModel();
    doc["psram"] = psramFound();

    doc["cry_detected"] = cry.detected;
    doc["cry_prob"] = cry.prob;
    doc["cry_state"] = cry.state;
    doc["cry_threshold"] = cry.threshold;
    doc["cry_duration_ms"] = cry.duration_ms;
    doc["cry_timestamp_ms"] = cry.timestamp_ms;

    doc["gps_valid"] = gps.valid;
    doc["gps_lat"] = gps.lat;
    doc["gps_lng"] = gps.lng;
    doc["gps_sats"] = gps.sats;

    doc["status_mode"] = st.mode;
    doc["status_battery"] = st.battery_percent;
    doc["status_wifi_rssi"] = st.wifi_rssi;
    doc["status_uptime_ms"] = st.uptime_ms;

    const AppState &as = app_state_get();
    doc["night_mode"] = as.nightMode;
    doc["indoor_mode"] = as.indoorMode;
    char profile[24];
    snprintf(profile, sizeof(profile), "%s|thr=%.2f", as.nightMode ? "night" : "day", cry.threshold);
    doc["config_profile"] = profile;
    doc["offline_buffered"] = offlineBuffered;

    char logLine[96];
    snprintf(logLine, sizeof(logLine), "Baby cry detected (prob=%.2f, mode=%s)", cry.prob, st.mode);
    doc["log"] = logLine;
    doc["occurred_at"] = isoTimestamp(cry.timestamp_ms ? cry.timestamp_ms : st.uptime_ms);

    String body;
    serializeJson(doc, body);

    bool ok = do_post(body);
    if (!ok)
    {
        LOGW("[BACKEND] First POST failed, retry once");
        ok = do_post(body);
    }
    return ok;
}

#include "AppState.h"

#include <ArduinoJson.h>
#include <cstring>

static AppState g_appState;

void app_state_init(const char *deviceId, const char *firmware)
{
    if (deviceId)
    {
        strncpy(g_appState.deviceId, deviceId, sizeof(g_appState.deviceId) - 1);
        g_appState.deviceId[sizeof(g_appState.deviceId) - 1] = '\0';
    }
    if (firmware)
    {
        strncpy(g_appState.firmware, firmware, sizeof(g_appState.firmware) - 1);
        g_appState.firmware[sizeof(g_appState.firmware) - 1] = '\0';
    }
}

void app_state_set_status(const char *msg)
{
    if (!msg)
        return;
    strncpy(g_appState.statusMessage, msg, sizeof(g_appState.statusMessage) - 1);
    g_appState.statusMessage[sizeof(g_appState.statusMessage) - 1] = '\0';
}

const char *app_state_get_status()
{
    return g_appState.statusMessage;
}

void app_state_set_wifi(bool connected, int rssi, const char *ssid, const char *ip)
{
    g_appState.wifi.connected = connected;
    g_appState.wifi.rssi = rssi;
    if (ssid)
    {
        strncpy(g_appState.wifi.ssid, ssid, sizeof(g_appState.wifi.ssid) - 1);
        g_appState.wifi.ssid[sizeof(g_appState.wifi.ssid) - 1] = '\0';
    }
    if (ip)
    {
        strncpy(g_appState.wifi.ip, ip, sizeof(g_appState.wifi.ip) - 1);
        g_appState.wifi.ip[sizeof(g_appState.wifi.ip) - 1] = '\0';
    }
}

void app_state_update_cry(float prob, float score, bool crying, uint32_t ts, const char *label)
{
    g_appState.cry.prob = prob;
    g_appState.cry.score = score;
    g_appState.cry.crying = crying;
    g_appState.cry.lastTs = ts;
    if (label)
    {
        strncpy(g_appState.cry.lastEvent, label, sizeof(g_appState.cry.lastEvent) - 1);
        g_appState.cry.lastEvent[sizeof(g_appState.cry.lastEvent) - 1] = '\0';
    }
}

void app_state_update_gps(double lat, double lng, float speed, uint8_t sats, bool valid, uint32_t ts)
{
    g_appState.gps.lat = lat;
    g_appState.gps.lng = lng;
    g_appState.gps.speed = speed;
    g_appState.gps.sats = sats;
    g_appState.gps.valid = valid;
    g_appState.gps.lastUpdateMs = ts;
}

void app_state_set_mode(bool nightMode, bool indoorMode)
{
    g_appState.nightMode = nightMode;
    g_appState.indoorMode = indoorMode;
}

String app_state_get_status_json()
{
    JsonDocument doc;
    doc["device_id"] = g_appState.deviceId;
    doc["fw"] = g_appState.firmware;
    doc["prob"] = g_appState.cry.prob;
    doc["score"] = g_appState.cry.score;
    doc["crying"] = g_appState.cry.crying;
    doc["last_event_ts"] = g_appState.cry.lastTs;
    doc["last_event"] = g_appState.cry.lastEvent;
    doc["lat"] = g_appState.gps.lat;
    doc["lng"] = g_appState.gps.lng;
    doc["gps_valid"] = g_appState.gps.valid;
    doc["sats"] = g_appState.gps.sats;
    doc["night_mode"] = g_appState.nightMode;
    doc["indoor_mode"] = g_appState.indoorMode;
    doc["status"] = g_appState.statusMessage;
    doc["ip_sta"] = g_appState.wifi.ip;
    doc["wifi_ssid"] = g_appState.wifi.ssid;
    doc["wifi_rssi"] = g_appState.wifi.rssi;

    String out;
    serializeJson(doc, out);
    return out;
}

const AppState &app_state_get()
{
    return g_appState;
}

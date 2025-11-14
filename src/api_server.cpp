#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "Config.h"

extern float g_lastProb;
extern float g_lastScore;
extern bool  g_isCrying;
extern double g_lastLat;
extern double g_lastLng;
extern bool   g_gpsValid;
extern char   g_statusMessage[64];
static WebServer server(80);

static void handleRoot(){ server.send(200, "text/plain", "AudioCry ESP32 - OK"); }
static void handleStatus(){
    String json="{";
    json += "\"device_id\":\""+String(DEVICE_ID)+"\",";
    json += "\"ip\":\""+WiFi.localIP().toString()+"\",";
    json += "\"prob\":"+String(g_lastProb,3)+",";
    json += "\"score\":"+String(g_lastScore,3)+",";
    json += "\"crying\":"; json += (g_isCrying ? "true":"false"); json += ",";
    json += "\"lat\":"+String(g_lastLat,6)+",";
    json += "\"lng\":"+String(g_lastLng,6)+",";
    json += "\"gps_valid\":"; json += (g_gpsValid ? "true":"false"); json += ",";
    json += "\"status\":\""+String(g_statusMessage)+"\"";
    json += "}";
    server.send(200, "application/json", json);
}
void api_begin(){ server.on("/", handleRoot); server.on("/status", handleStatus); server.begin(); }
void api_loop(){ server.handleClient(); }

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "Config.h"
#include "WifiConfig.h"
#include "wifi_service.h"

extern float g_lastProb;
extern float g_lastScore;
extern bool  g_isCrying;
extern double g_lastLat;
extern double g_lastLng;
extern bool   g_gpsValid;
extern char   g_statusMessage[64];
extern bool   nightMode;
extern const char* g_lastEvent;
extern uint32_t g_lastEventTs;

static WebServer server(80);

static void handleRoot(){
    server.send(200, "text/plain", "AudioCry ESP32 - OK");
}

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
    json += "\"status\":\""+String(g_statusMessage)+"\",";
    json += "\"night_mode\":"; json += (nightMode ? "true":"false"); json += ",";
    json += "\"last_event\":\""+String(g_lastEvent)+"\",";
    json += "\"last_event_ts\":"; json += String(g_lastEventTs);
    json += "}";
    server.send(200, "application/json", json);
}

static String htmlEscape(const String& in){
    String out;
    out.reserve(in.length());
    for (size_t i=0;i<in.length();++i){
        char c = in[i];
        switch(c){
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            case '\'': out += F("&#39;"); break;
            default: out += c; break;
        }
    }
    return out;
}

static void handleWifiConfig(){
    WifiCredentials creds;
    wifi_config_load(creds);
    WifiCredentials updated = creds;
    String message;
    if (server.method() == HTTP_POST){
        String action = server.arg("action");
        if (action == "delete"){
            wifi_config_clear();
            creds = WifiCredentials{};
            updated = creds;
            message = F("Da xoa WiFi da luu.");
            wifi_request_reconnect();
        } else {
            updated.ssid = server.arg("ssid");
            updated.pass = server.arg("pass");
            updated.ssid.trim();
            wifi_config_save(updated);
            wifi_request_reconnect();
            message = F("Đã lưu WiFi, đợi vài giây để kết nối lại.");
            creds = updated;
        }
    }
    String html = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>AudioCry WiFi</title><style>body{font-family:sans-serif;margin:2rem;}form{max-width:420px;padding:1rem;border:1px solid #ccc;border-radius:8px;}label{display:block;margin-top:0.8rem;font-weight:600;}input{width:100%;padding:0.4rem;margin-top:0.2rem;}button{margin-top:1rem;padding:0.6rem 1.2rem;} .msg{padding:0.6rem;background:#eef;border:1px solid #77f;border-radius:6px;margin-bottom:1rem;}</style></head><body>");
    html += F("<h2>AudioCry ESP32 - Cấu hình WiFi</h2>");
    if (message.length()){
        html += "<div class='msg'>"+message+"</div>";
    }
    if (WiFi.status() == WL_CONNECTED){
        html += "<p>Đang kết nối: <strong>"+htmlEscape(WiFi.SSID())+"</strong> (IP "+WiFi.localIP().toString()+")</p>";
    } else {
        html += F("<p>Chưa kết nối WiFi.</p>");
    }
    if (wifi_is_setup_ap_active()){
        html += "<p>AP cấu hình: <strong>"+htmlEscape(String(wifi_get_setup_ap_ssid()))+"</strong> (mật khẩu: "+String(wifi_get_setup_ap_pass())+")</p>";
    }
    html += "<section><h3>Saved Wi-Fi</h3>";
    if (creds.ssid.length()){
        html += "<div><strong>"+htmlEscape(creds.ssid)+"</strong>";
        html += F(" <form method='POST' style='display:inline;margin-left:0.5rem;'>");
        html += F("<input type='hidden' name='action' value='delete'>");
        html += F("<button type='submit'>Xoa</button></form></div>");
    } else {
        html += F("<p>Chưa có Wi-Fi nào được lưu.</p>");
    }
    html += F("</section>");
    html += F("<form method='POST'>");
    html += "<label>SSID</label><input name='ssid' maxlength='32' value='"+htmlEscape(creds.ssid)+"' placeholder='Tên WiFi'>";
    html += "<label>Mật khẩu</label><input type='password' name='pass' maxlength='63' value='"+htmlEscape(creds.pass)+"' placeholder='Mật khẩu'>";
    html += F("<button type='submit'>Lưu</button></form>");
    html += F("</body></html>");
    server.send(200, "text/html", html);
}


void api_begin(){
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/wifi", HTTP_GET, handleWifiConfig);
    server.on("/wifi", HTTP_POST, handleWifiConfig);
    server.begin();
}

void api_loop(){
    server.handleClient();
}

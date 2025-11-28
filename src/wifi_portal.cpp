#include "wifi_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include "WifiConfig.h"
#include "AppState.h"
#include "wifi_service.h"

static String htmlEscape(const String &in)
{
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i)
    {
        char c = in[i];
        switch (c)
        {
        case '&':
            out += F("&amp;");
            break;
        case '<':
            out += F("&lt;");
            break;
        case '>':
            out += F("&gt;");
            break;
        case '"':
            out += F("&quot;");
            break;
        case '\'':
            out += F("&#39;");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static void handleWifiConfig(WebServer &server)
{
    WifiCredentials creds;
    wifi_config_load(creds);
    bool hasStored = wifi_config_has_credentials();
    WifiCredentials updated = creds;
    String message;

    if (server.method() == HTTP_POST)
    {
        String action = server.arg("action");
        if (action == "delete")
        {
            wifi_config_clear();
            updated = WifiCredentials{};
            hasStored = false;
            wifi_clear_no_cred_pause();
            wifi_request_reconnect();
            message = F("Da xoa WiFi da luu.");
        }
        else
        {
            updated.ssid = server.arg("ssid");
            updated.pass = server.arg("pass");
            updated.ssid.trim();
            updated.pass.trim();
            if (updated.ssid.isEmpty())
            {
                message = F("SSID khong duoc de trong.");
            }
            else
            {
                wifi_config_save(updated);
                hasStored = wifi_config_has_credentials();
                wifi_clear_no_cred_pause();
                wifi_request_reconnect();
                message = F("Da luu WiFi, thiet bi se thu ket noi ngay.");
            }
        }
    }

    bool staConnected = (WiFi.status() == WL_CONNECTED);
    String html = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>AudioCry WiFi</title><style>body{font-family:sans-serif;margin:2rem;}form{max-width:420px;padding:1rem;border:1px solid #ccc;border-radius:8px;}label{display:block;margin-top:0.8rem;font-weight:600;}input{width:100%;padding:0.4rem;margin-top:0.2rem;}button{margin-top:1rem;padding:0.6rem 1.2rem;} .msg{padding:0.6rem;background:#eef;border:1px solid #77f;border-radius:6px;margin-bottom:1rem;}</style></head><body>");
    html += F("<h2>AudioCry ESP32 - Cau hinh WiFi</h2>");
    if (message.length())
    {
        html += "<div class='msg'>" + message + "</div>";
    }

    html += "<p>Trang thai: <strong>" + htmlEscape(String(app_state_get_status())) + "</strong></p>";
    if (staConnected)
    {
        html += "<p>Dang ket noi: <strong>" + htmlEscape(WiFi.SSID()) + "</strong> (IP " + WiFi.localIP().toString() + ")</p>";
    }
    else
    {
        html += F("<p>Chua ket noi WiFi.</p>");
    }

    if (wifi_is_setup_ap_active())
    {
        html += "<p>AP cau hinh: <strong>" + htmlEscape(String(wifi_get_setup_ap_ssid())) + "</strong> (pass \"" + htmlEscape(String(wifi_get_setup_ap_pass())) + "\", IP " + WiFi.softAPIP().toString() + ")</p>";
    }

    html += F("<section><h3>Saved Wi-Fi</h3>");
    if (hasStored && updated.ssid.length())
    {
        html += "<div><strong>" + htmlEscape(updated.ssid) + "</strong>";
        html += F(" <form method='POST' style='display:inline;margin-left:0.5rem;'>");
        html += F("<input type='hidden' name='action' value='delete'>");
        html += F("<button type='submit'>Xoa</button></form></div>");
    }
    else if (updated.ssid.length())
    {
        html += "<p>Dang dung SSID mac dinh: <strong>" + htmlEscape(updated.ssid) + "</strong></p>";
    }
    else
    {
        html += F("<p>Chua luu Wi-Fi nao.</p>");
    }
    html += F("</section>");

    html += F("<form method='POST'>");
    html += "<label>SSID</label><input name='ssid' maxlength='32' value='" + htmlEscape(updated.ssid) + "' placeholder='Ten WiFi'>";
    html += "<label>Mat khau</label><input type='password' name='pass' maxlength='63' value='" + htmlEscape(updated.pass) + "' placeholder='Mat khau'>";
    html += F("<button type='submit'>Luu</button></form>");
    html += F("</body></html>");
    server.send(200, "text/html", html);
}

static WebServer *s_server = nullptr;

static void handleWifiConfigGet()
{
    if (s_server)
        handleWifiConfig(*s_server);
}
static void handleWifiConfigPost()
{
    if (s_server)
        handleWifiConfig(*s_server);
}

void wifi_portal_register(WebServer &server)
{
    s_server = &server;
    server.on("/wifi", HTTP_GET, handleWifiConfigGet);
    server.on("/wifi", HTTP_POST, handleWifiConfigPost);
}

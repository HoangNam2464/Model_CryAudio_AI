#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <SPIFFS.h>
#include <FS.h>
#include <HardwareSerial.h>
#include <cstdarg>
#include <cstdio>
#include <WebServer.h>
#include <esp32-hal-psram.h>

// Định nghĩa cổng log Serial0 (UART0)
HardwareSerial Serial0(0);

#include "Config.h"
#include "board_config.h"
#include "WifiConfig.h"
#include <CryDetector.h>
#include <RestClient.h>
#include <tflm_infer.h>

// ====== Cờ bật/tắt AudioCry ======
// Để test Wi-Fi + GPS trên DevKit không PSRAM, tắt AudioCry nếu thiếu RAM.
static constexpr bool ENABLE_AUDIOCRY = true;
// Bật âm báo loa khi đổi night mode dù không chạy AI/mic (dùng I2S TX đơn giản)
static constexpr bool ENABLE_SPEAKER_FEEDBACK = true;

// ====== Wi-Fi cấu hình ======
static const uint32_t WIFI_BACKOFF_MIN_MS = 1000;
static const uint32_t WIFI_BACKOFF_MAX_MS = 30000;
static const uint32_t WIFI_AP_FALLBACK_MS = 20000; // sau 20s mất link -> bật AP tạm
static const char *SETUP_AP_SSID = "AudioCry-Setup";
static const char *SETUP_AP_PASS = "12345678";

// ====== GPS cấu hình (đã khai báo trong Config.h/board_config.h) ======

// ====== I2S mic/loa (dùng nếu ENABLE_AUDIOCRY) ======
static const int MIC_SD = I2S_SD_PIN;
static const int MIC_WS = I2S_WS_PIN;
static const int MIC_SCK = I2S_SCK_PIN;
static const int SPK_DIN = I2S_SD_OUT_PIN;
static const int SPK_BCLK = SPK_I2S_BCLK_PIN;
static const int SPK_LRCLK = SPK_I2S_LRCK_PIN;

// ====== FreeRTOS handles ======
TaskHandle_t hWifi = nullptr;
TaskHandle_t hGps = nullptr;
TaskHandle_t hApp = nullptr;
TaskHandle_t hMic = nullptr;
TaskHandle_t hInfer = nullptr;
TaskHandle_t hSpeaker = nullptr;
TaskHandle_t hSender = nullptr;

// ====== Queues ======
static QueueHandle_t qPcm = nullptr;
static QueueHandle_t qEvents = nullptr;
static QueueHandle_t qSpeaker = nullptr;

// ====== GPS data ======
struct GpsData
{
    double lat = 0;
    double lon = 0;
    float speed = 0;
    uint8_t sats = 0;
    bool fix = false;
    uint32_t lastUpdateMs = 0;
};
static GpsData g_gps;
static SemaphoreHandle_t gGpsMutex;

// ====== Wi-Fi state ======
static bool g_wifiConnected = false;
static uint32_t g_lastWifiLostMs = 0;
static bool g_apActive = false;
static bool g_wifiReconnectRequested = false;
static WifiCredentials g_wifiCreds;
static WebServer g_webServer(80);
static bool g_psramOk = false;

// ====== Cry event ======
struct CryEvent
{
    bool crying;
    bool heartbeat;
    float prob;
    float score;
    double lat;
    double lon;
    bool gpsValid;
    uint32_t tsMs;
    uint32_t durationMs;
};

// ====== Audio / AI ======
static constexpr uint32_t PCM_POOL_BUFFERS = 3;
static constexpr size_t TARGET_SAMPLES = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S);
static int16_t *g_inferWindow = nullptr;
static uint32_t g_pcmBuffersAllocated = 0;
static bool nightMode = false;
static float g_lastProb = 0.0f;
static float g_lastScore = 0.0f;
static bool g_isCrying = false;
static char g_lastEvent[16] = "boot";
static uint32_t g_lastEventTs = 0;
static double g_lastLat = 0;
static double g_lastLng = 0;
static bool g_gpsValid = false;
static char g_statusMessage[64] = "Booting";
// ==== Speaker helpers ====
static void playBeep(int freq, int durationMs, int volume);
static void playVoice(const char *filename);

// ==== Logging on Serial0 ====
void log0(const String &msg)
{
    Serial0.println(msg);
}
void logf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial0.println(buf);
}
void setStatusMessage(const char *msg)
{
    if (!msg)
        return;
    strncpy(g_statusMessage, msg, sizeof(g_statusMessage) - 1);
    g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';
}
static void updateWifiLed(bool connected)
{
    pinMode(LED_WIFI_PIN, OUTPUT);
#if LED_WIFI_ACTIVE_LOW
    digitalWrite(LED_WIFI_PIN, connected ? LOW : HIGH);
#else
    digitalWrite(LED_WIFI_PIN, connected ? HIGH : LOW);
#endif
}
static void updateCryLed(bool crying)
{
    pinMode(LED_CRY_RED_PIN, OUTPUT);
    pinMode(LED_CRY_GREEN_PIN, OUTPUT);
    // LED do sang khi be khoc, LED xanh la sang khi be yen
    digitalWrite(LED_CRY_RED_PIN, crying ? HIGH : LOW);
    digitalWrite(LED_CRY_GREEN_PIN, crying ? LOW : HIGH);
}
static void updateNightLed()
{
    pinMode(LED_NIGHT_PIN, OUTPUT);
    // LED trang: bat khi ban ngay, tat khi ban dem
    digitalWrite(LED_NIGHT_PIN, nightMode ? LOW : HIGH);
}
// Cry detector profile (ngày/đêm)
struct DetectorProfile
{
    float onTh;
    float offTh;
    float stableOn;
    float stableOff;
    float minOn;
    float minOff;
};
static CryDetector detector(
    0.70f, 0.18f, 0.15f,
    1.0f, 2.3f,
    2.0f, 1.5f,
    0.25f,
    INFER_INTERVAL_S);
static constexpr DetectorProfile DAY_PROFILE{
    0.70f, 0.18f, 1.0f, 2.3f, 2.0f, 1.5f};
static constexpr DetectorProfile NIGHT_PROFILE{
    0.78f, 0.22f, 1.5f, 2.8f, 2.5f, 2.0f};

// Speaker event
enum class SpeakerEvent : uint8_t
{
    EVENT_MODE_DAY,
    EVENT_MODE_NIGHT,
    EVENT_CRY_DAY,   // Voice: "Em bé đang khóc"
    EVENT_CALM_DAY,  // Voice: "Em bé ngủ yên"
    EVENT_CRY_NIGHT, // Beep nhỏ
    EVENT_CALM_NIGHT // Beep rất nhẹ
};

// Forward declarations
void setupWiFi();
void setupGPS();
void setupAudioCry();
void setupSpeakerFeedback();
void setupWebServer();
void checkPsram();
void taskWifi(void *param);
void taskGps(void *param);
void taskApp(void *param);
void taskMic(void *param);
void taskInfer(void *param);
void taskSpeaker(void *param);
void taskSender(void *param);
void taskWeb(void *param);
bool parseNMEA(const char *line, size_t len, GpsData &out);
void startSetupAP();
void stopSetupAP();
void applyDetectorProfile();

// ==== CryDetector profile ====
void applyDetectorProfile()
{
    const DetectorProfile &p = nightMode ? NIGHT_PROFILE : DAY_PROFILE;
    detector.configure(p.onTh, p.offTh, p.stableOn, p.stableOff, p.minOn, p.minOff);
}

// ==== Wi-Fi event callback ====
void handleWiFiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        log0("[WiFi] Connected to AP");
        updateWifiLed(true);
        setStatusMessage("WiFi ket noi, cho IP...");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        g_wifiConnected = true;
        stopSetupAP();
        logf("[WiFi] IP: %s", WiFi.localIP().toString().c_str());
        updateWifiLed(true);
        setStatusMessage("WiFi da ket noi");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        g_wifiConnected = false;
        g_lastWifiLostMs = millis();
        log0("[WiFi] Lost");
        updateWifiLed(false);
        setStatusMessage("Mat WiFi, se fallback AP neu can.");
        break;
    default:
        break;
    }
}

// ==== Setup Wi-Fi ====
void setupWiFi()
{
    wifi_config_init();
    wifi_config_load(g_wifiCreds);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(handleWiFiEvent);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    g_lastWifiLostMs = millis();
    if (g_wifiCreds.ssid.isEmpty())
    {
        log0("[WiFi] No Wi-Fi credentials saved, enabling setup AP");
        setStatusMessage("Chua co WiFi, dung AP cau hinh.");
        startSetupAP();
    }
}

// ==== AP fallback ====
void startSetupAP()
{
    if (g_apActive)
        return;
    if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    if (WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS))
    {
        g_apActive = true;
        IPAddress ip = WiFi.softAPIP();
        logf("[WiFi] AP fallback: %s / %s (IP %s)", SETUP_AP_SSID, SETUP_AP_PASS, ip.toString().c_str());
        setStatusMessage("Dang mo AP cau hinh WiFi.");
    }
    else
    {
        log0("[WiFi] Failed to start AP fallback");
    }
}

void stopSetupAP()
{
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    g_apActive = false;
}

// ==== PSRAM check ====
void checkPsram()
{
    g_psramOk = psramInit() && psramFound();
    if (g_psramOk)
    {
        size_t psSize = ESP.getPsramSize();
        size_t psFree = ESP.getFreePsram();
        logf("[PSRAM] OK size=%u free=%u", (unsigned)psSize, (unsigned)psFree);
    }
    else
    {
        log0("[PSRAM] Not detected. AI may fail; please use S3 with PSRAM or reduce model.");
        setStatusMessage("PSRAM khong thay, AI co the tat.");
    }
}

// ==== Web server (status + Wi-Fi config) ====
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

static void handleRoot()
{
    g_webServer.send(200, "text/plain", "AudioCry ESP32 - OK");
}

static void handleStatusJson()
{
    String json = "{";
    json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    json += "\"ip_sta\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ip_ap\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"prob\":" + String(g_lastProb, 3) + ",";
    json += "\"score\":" + String(g_lastScore, 3) + ",";
    json += "\"crying\":";
    json += (g_isCrying ? "true" : "false");
    json += ",";
    json += "\"lat\":" + String(g_lastLat, 6) + ",";
    json += "\"lng\":" + String(g_lastLng, 6) + ",";
    json += "\"gps_valid\":";
    json += (g_gpsValid ? "true" : "false");
    json += ",";
    json += "\"status\":\"" + String(g_statusMessage) + "\",";
    json += "\"night_mode\":";
    json += (nightMode ? "true" : "false");
    json += ",";
    json += "\"last_event\":\"" + String(g_lastEvent) + "\",";
    json += "\"last_event_ts\":";
    json += String(g_lastEventTs);
    json += "}";
    g_webServer.send(200, "application/json", json);
}

static void handleWifiConfig()
{
    WifiCredentials creds;
    wifi_config_load(creds);
    WifiCredentials updated = creds;
    String message;
    if (g_webServer.method() == HTTP_POST)
    {
        String action = g_webServer.arg("action");
        if (action == "delete")
        {
            wifi_config_clear();
            creds = WifiCredentials{};
            updated = creds;
            g_wifiCreds = updated;
            g_wifiReconnectRequested = true;
            g_lastWifiLostMs = millis();
            startSetupAP();
            setStatusMessage("Chua co WiFi, dung AP cau hinh.");
            message = F("Da xoa WiFi da luu.");
        }
        else
        {
            updated.ssid = g_webServer.arg("ssid");
            updated.pass = g_webServer.arg("pass");
            updated.ssid.trim();
            wifi_config_save(updated);
            g_wifiCreds = updated;
            g_wifiReconnectRequested = true;
            g_lastWifiLostMs = millis();
            startSetupAP();
            setStatusMessage("Da luu WiFi moi, dang ket noi...");
            message = F("Da luu WiFi, doi vai giay de ket noi lai.");
        }
    }
    String html = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>AudioCry WiFi</title><style>body{font-family:sans-serif;margin:2rem;}form{max-width:420px;padding:1rem;border:1px solid #ccc;border-radius:8px;}label{display:block;margin-top:0.8rem;font-weight:600;}input{width:100%;padding:0.4rem;margin-top:0.2rem;}button{margin-top:1rem;padding:0.6rem 1.2rem;} .msg{padding:0.6rem;background:#eef;border:1px solid #77f;border-radius:6px;margin-bottom:1rem;}</style></head><body>");
    html += F("<h2>AudioCry ESP32 - Cau hinh WiFi</h2>");
    if (message.length())
    {
        html += "<div class='msg'>" + message + "</div>";
    }
    html += "<p>Trang thai: <strong>" + htmlEscape(String(g_statusMessage)) + "</strong></p>";
    if (g_wifiConnected)
    {
        html += "<p>Dang ket noi: <strong>" + htmlEscape(WiFi.SSID()) + "</strong> (IP " + WiFi.localIP().toString() + ")</p>";
    }
    else
    {
        html += F("<p>Chua ket noi WiFi.</p>");
    }
    if (g_apActive)
    {
        html += "<p>AP cau hinh: <strong>" + htmlEscape(String(SETUP_AP_SSID)) + "</strong> (pass " + String(SETUP_AP_PASS) + ", IP " + WiFi.softAPIP().toString() + ")</p>";
    }
    html += F("<section><h3>Saved Wi-Fi</h3>");
    if (creds.ssid.length())
    {
        html += "<div><strong>" + htmlEscape(creds.ssid) + "</strong>";
        html += F(" <form method='POST' style='display:inline;margin-left:0.5rem;'>");
        html += F("<input type='hidden' name='action' value='delete'>");
        html += F("<button type='submit'>Xoa</button></form></div>");
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
    g_webServer.send(200, "text/html", html);
}

void setupWebServer()
{
    g_webServer.on("/", handleRoot);
    g_webServer.on("/status", handleStatusJson);
    g_webServer.on("/wifi", HTTP_GET, handleWifiConfig);
    g_webServer.on("/wifi", HTTP_POST, handleWifiConfig);
    g_webServer.begin();
    log0("[Web] HTTP server started on :80");
}

void taskWeb(void *param)
{
    for (;;)
    {
        g_webServer.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==== Setup GPS ====
void setupGPS()
{
    Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    log0("[GPS] Serial2 started");
}

// ==== Setup AudioCry (I2S) ====
void setupAudioCry()
{
    bool needMic = ENABLE_AUDIOCRY;
    bool needSpeaker = ENABLE_AUDIOCRY || ENABLE_SPEAKER_FEEDBACK || USE_MAX98357A_SPK;
    if (!needMic && !needSpeaker)
        return;

    i2s_config_t cfg = {};
    cfg.mode = I2S_MODE_MASTER;
    if (needMic)
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_RX);
    if (needSpeaker)
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX);
    cfg.sample_rate = I2S_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = I2S_READ_LEN;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = needSpeaker;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = MIC_SCK;
    pins.ws_io_num = MIC_WS;
    pins.data_out_num = needSpeaker ? SPK_DIN : I2S_PIN_NO_CHANGE;
    pins.data_in_num = needMic ? MIC_SD : I2S_PIN_NO_CHANGE;

    i2s_driver_install(MIC_I2S_PORT, &cfg, 0, nullptr);
    i2s_set_pin(MIC_I2S_PORT, &pins);
    i2s_set_clk(MIC_I2S_PORT, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

    if (ENABLE_AUDIOCRY)
    {
        g_inferWindow = (int16_t *)heap_caps_malloc(TARGET_SAMPLES * sizeof(int16_t),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_inferWindow)
        {
            g_inferWindow = (int16_t *)heap_caps_malloc(TARGET_SAMPLES * sizeof(int16_t),
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
}
// ==== Task Wi-Fi ====
void taskWifi(void *param)
{
    uint32_t backoff = WIFI_BACKOFF_MIN_MS;
    log0("[WiFiTask] start");
    for (;;)
    {
        uint32_t now = millis();
        if (g_wifiReconnectRequested)
        {
            g_wifiReconnectRequested = false;
            WiFi.disconnect(true, true);
            g_wifiConnected = false;
            g_lastWifiLostMs = now;
            backoff = WIFI_BACKOFF_MIN_MS;
        }
        if (!g_wifiConnected)
        {
            if (g_wifiCreds.ssid.isEmpty())
            {
                if (!g_apActive)
                {
                    startSetupAP();
                }
                setStatusMessage("Chua co WiFi, dung AP cau hinh.");
                vTaskDelay(pdMS_TO_TICKS(400));
                continue;
            }

            if (WiFi.getMode() == WIFI_AP)
            {
                WiFi.mode(WIFI_AP_STA);
            }

            logf("[WiFi] Connecting to %s...", g_wifiCreds.ssid.c_str());
            setStatusMessage("Dang ket noi WiFi...");
            WiFi.begin(g_wifiCreds.ssid.c_str(), g_wifiCreds.pass.c_str());

            uint32_t t0 = millis();
            while (!g_wifiConnected && (millis() - t0) < backoff)
            {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (g_wifiConnected)
            {
                log0("[WiFi] Connected");
                backoff = WIFI_BACKOFF_MIN_MS;
            }
            else
            {
                logf("[WiFi] Retry in %u ms", backoff);
                if (!g_apActive || (millis() - g_lastWifiLostMs) > WIFI_AP_FALLBACK_MS)
                {
                    startSetupAP();
                }
                g_lastWifiLostMs = millis();
                backoff = std::min(backoff * 2, WIFI_BACKOFF_MAX_MS);
            }
        }
        now = millis();
        if (!g_wifiConnected && (now - g_lastWifiLostMs) > WIFI_AP_FALLBACK_MS)
        {
            startSetupAP();
        }
        static uint32_t lastLog = 0;
        if (g_wifiConnected && now - lastLog > 5000)
        {
            log0("[WiFi] Link healthy");
            lastLog = now;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
// ==== Task GPS ====
void taskGps(void *param)
{
    log0("[GPS] task started");
    char lineBuf[160];
    size_t idx = 0;
    uint32_t lastRx = millis();
    uint32_t lastAnyNmeaMs = millis();
    uint32_t lastSatsZeroMs = millis();
    for (;;)
    {
        while (Serial2.available())
        {
            char c = Serial2.read();
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                if (idx > 6)
                {
                    GpsData tmp;
                    if (parseNMEA(lineBuf, idx, tmp))
                    {
                        if (xSemaphoreTake(gGpsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
                        {
                            g_gps = tmp;
                            g_gps.lastUpdateMs = millis();
                            xSemaphoreGive(gGpsMutex);
                        }
                        lastAnyNmeaMs = millis();
                        if (tmp.sats == 0)
                        {
                            // nếu liên tục sats=0 quá 30s sẽ log cảnh báo
                            if (millis() - lastSatsZeroMs > 30000)
                            {
                                log0("[GPS] Sats=0 >30s, kiểm tra anten/vị trí");
                                lastSatsZeroMs = millis();
                            }
                        }
                    }
                }
                idx = 0;
            }
            else
            {
                if (idx < sizeof(lineBuf) - 1)
                {
                    lineBuf[idx++] = c;
                    lineBuf[idx] = '\0';
                }
            }
            lastRx = millis();
        }
        if (millis() - lastRx > 5000)
        {
            log0("[GPS] No data >5s");
            lastRx = millis();
        }
        if (millis() - lastAnyNmeaMs > 30000)
        {
            log0("[GPS] Dữ liệu bất thường, không thấy GGA/RMC >30s. Kiểm tra anten/chân RX/TX.");
            lastAnyNmeaMs = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ==== Task App: log GPS, handle button night mode ====
void taskApp(void *param)
{
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_NIGHT_PIN, OUTPUT);
    updateNightLed();
    bool lastBtn = HIGH;
    uint32_t lastChange = 0;
    uint32_t lastLog = 0;
    for (;;)
    {
        // log GPS mỗi 5s
        uint32_t now = millis();
        if (now - lastLog > 5000)
        {
            GpsData snapshot;
            if (xSemaphoreTake(gGpsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                snapshot = g_gps;
                xSemaphoreGive(gGpsMutex);
            }
            g_lastLat = snapshot.lat;
            g_lastLng = snapshot.lon;
            g_gpsValid = snapshot.fix;
            logf("[GPS] Lat: %.6f, Lon: %.6f, Speed: %.2f, Sat: %u, Fix: %s",
                 snapshot.lat, snapshot.lon, snapshot.speed,
                 snapshot.sats, snapshot.fix ? "OK" : "NO");
            lastLog = now;
        }
        // toggle night mode
        bool btn = digitalRead(MODE_BUTTON_PIN) == LOW;
        uint32_t nowMs = millis();
        if (btn && !lastBtn && (nowMs - lastChange) > 50)
        {
            lastChange = nowMs;
            nightMode = !nightMode;
            applyDetectorProfile();
            if (qSpeaker)
            {
                SpeakerEvent ev = nightMode ? SpeakerEvent::EVENT_MODE_NIGHT : SpeakerEvent::EVENT_MODE_DAY;
                xQueueSend(qSpeaker, &ev, 0);
            }
            updateNightLed();
            // Nhay LED Wi-Fi (LED xanh duong tren board) de bao doi che do
            digitalWrite(LED_WIFI_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(LED_WIFI_PIN, LOW);
            // Khoi phuc trang thai LED Wi-Fi theo ket noi hien tai
            updateWifiLed(g_wifiConnected);
            logf("[MODE] nightMode=%s", nightMode ? "ON" : "OFF");
        }
        lastBtn = btn;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==== PCM buffer pool ====
static bool initPcmPool()
{
    qPcm = xQueueCreate(PCM_POOL_BUFFERS, sizeof(int16_t *));
    if (!qPcm)
        return false;
    uint32_t allocated = 0;
    for (uint32_t i = 0; i < PCM_POOL_BUFFERS; ++i)
    {
        int16_t *buf = (int16_t *)heap_caps_malloc(I2S_READ_LEN * sizeof(int16_t),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf)
        {
            buf = (int16_t *)heap_caps_malloc(I2S_READ_LEN * sizeof(int16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf)
            break;
        xQueueSend(qPcm, &buf, 0);
        allocated++;
    }
    g_pcmBuffersAllocated = allocated;
    return allocated > 0;
}

static inline int16_t *acquirePcmBlock(TickType_t timeoutTicks)
{
    int16_t *block = nullptr;
    if (qPcm && xQueueReceive(qPcm, &block, timeoutTicks) == pdTRUE)
        return block;
    return nullptr;
}

static inline void releasePcmBlock(int16_t *block)
{
    if (!block)
        return;
    if (!qPcm || xQueueSend(qPcm, &block, 0) != pdTRUE)
    {
        heap_caps_free(block);
    }
}

// ==== Task Mic ====
void taskMic(void *param)
{
    esp_task_wdt_add(nullptr);
    size_t bytes_read = 0;
    uint32_t blocksOk = 0;
    for (;;)
    {
        esp_task_wdt_reset();
        int16_t *block = acquirePcmBlock(pdMS_TO_TICKS(50));
        if (!block)
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        // Không để i2s_read block quá lâu để tránh WDT
        esp_err_t err = i2s_read(MIC_I2S_PORT,
                                 block,
                                 I2S_READ_LEN * sizeof(int16_t),
                                 &bytes_read,
                                 pdMS_TO_TICKS(50));
        if (err != ESP_OK || bytes_read != I2S_READ_LEN * sizeof(int16_t))
        {
            releasePcmBlock(block);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (xQueueSend(qPcm, &block, 0) != pdTRUE)
        {
            releasePcmBlock(block);
        }
        blocksOk++;
        esp_task_wdt_reset();
    }
}

// ==== Task Speaker ====
static void playBeep(int freq, int durationMs, int volume)
{
    const size_t N = static_cast<size_t>(I2S_SAMPLE_RATE * (durationMs / 1000.0f));
    static std::vector<int16_t> buf;
    buf.resize(N);
    for (size_t i = 0; i < N; ++i)
    {
        float env = sinf(3.14159f * i / N);
        float s = sinf(2.0f * 3.14159f * freq * i / I2S_SAMPLE_RATE) * env;
        buf[i] = static_cast<int16_t>(s * volume);
    }
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(buf.data());
    size_t bytesRemaining = buf.size() * sizeof(int16_t);
    while (bytesRemaining > 0)
    {
        size_t written = 0;
        i2s_write(SPK_I2S_PORT, ptr, bytesRemaining, &written, pdMS_TO_TICKS(200));
        if (written == 0)
            break;
        ptr += written;
        bytesRemaining -= written;
    }
}

static void playVoice(const char *filename)
{
    if (!SPIFFS.begin(true))
    {
        log0("[SPK] SPIFFS mount failed");
        return;
    }
    File f = SPIFFS.open(filename, "r");
    if (!f)
    {
        logf("[SPK] Voice file missing: %s", filename);
        return;
    }
    uint8_t header[44];
    if (f.read(header, sizeof(header)) != sizeof(header))
    {
        log0("[SPK] WAV header read fail");
        f.close();
        return;
    }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
    {
        log0("[SPK] Not a WAV file");
        f.close();
        return;
    }
    const size_t chunk = 1024;
    static std::vector<uint8_t> wavBuf;
    wavBuf.resize(chunk);
    while (true)
    {
        size_t r = f.read(wavBuf.data(), chunk);
        if (r == 0)
            break;
        size_t written = 0;
        i2s_write(SPK_I2S_PORT, wavBuf.data(), r, &written, pdMS_TO_TICKS(200));
        if (written == 0)
            break;
    }
    f.close();
}

void taskSpeaker(void *param)
{
    SpeakerEvent ev = SpeakerEvent::EVENT_CALM_DAY;
    for (;;)
    {
        if (xQueueReceive(qSpeaker, &ev, portMAX_DELAY) == pdTRUE)
        {
            switch (ev)
            {
            case SpeakerEvent::EVENT_MODE_DAY:
                playBeep(1000, 80, 4000);
                break;
            case SpeakerEvent::EVENT_MODE_NIGHT:
                playBeep(800, 80, 3000);
                break;
            case SpeakerEvent::EVENT_CRY_DAY:
                playVoice("/cry_day.wav");
                break;
            case SpeakerEvent::EVENT_CALM_DAY:
                playVoice("/calm_day.wav");
                break;
            case SpeakerEvent::EVENT_CRY_NIGHT:
                playBeep(700, 120, 2500);
                break;
            case SpeakerEvent::EVENT_CALM_NIGHT:
                playBeep(600, 80, 1800);
                break;
            default:
                break;
            }
        }
    }
}

// ==== Task Infer ====
void taskInfer(void *param)
{
    if (!g_inferWindow)
    {
        log0("[AI] No infer buffer");
        vTaskDelete(nullptr);
    }
    int retry = 0;
    const int kMaxRetry = 6;
    while (!tflm_begin())
    {
        retry++;
        log0("[AI] Failed to init TFLM, retrying...");
        if (retry >= kMaxRetry)
        {
            log0("[AI] Disable AI (init failed / low RAM). Dùng S3 PSRAM hoặc giảm arena.");
            vTaskDelete(nullptr);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    applyDetectorProfile();
    size_t filled = 0;
    bool prevState = false;
    for (;;)
    {
        while (filled < TARGET_SAMPLES)
        {
            int16_t *block = acquirePcmBlock(pdMS_TO_TICKS(10));
            if (block)
            {
                size_t copy = std::min(TARGET_SAMPLES - filled, (size_t)I2S_READ_LEN);
                memcpy(g_inferWindow + filled, block, copy * sizeof(int16_t));
                filled += copy;
                releasePcmBlock(block);
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        float prob = tflm_infer_prob(g_inferWindow, TARGET_SAMPLES);
        bool state = detector.update(prob);
        float score = detector.score();
        g_lastProb = prob;
        g_lastScore = score;
        g_isCrying = state;
        if (state != prevState)
        {
            updateCryLed(state);
            if (qSpeaker)
            {
                SpeakerEvent ev;
                if (nightMode)
                    ev = state ? SpeakerEvent::EVENT_CRY_NIGHT : SpeakerEvent::EVENT_CALM_NIGHT;
                else
                    ev = state ? SpeakerEvent::EVENT_CRY_DAY : SpeakerEvent::EVENT_CALM_DAY;
                xQueueSend(qSpeaker, &ev, 0);
            }
            CryEvent evt{};
            evt.crying = state;
            evt.heartbeat = false;
            evt.prob = prob;
            evt.score = score;
            evt.tsMs = millis();
            GpsData snapshot{};
            if (xSemaphoreTake(gGpsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                snapshot = g_gps;
                xSemaphoreGive(gGpsMutex);
            }
            evt.lat = snapshot.lat;
            evt.lon = snapshot.lon;
            evt.gpsValid = snapshot.fix;
            evt.durationMs = static_cast<uint32_t>(INFER_INTERVAL_S * 1000);
            g_lastLat = snapshot.lat;
            g_lastLng = snapshot.lon;
            g_gpsValid = snapshot.fix;
            strncpy(g_lastEvent, state ? "cry_on" : "cry_off", sizeof(g_lastEvent) - 1);
            g_lastEvent[sizeof(g_lastEvent) - 1] = '\0';
            g_lastEventTs = evt.tsMs;
            if (qEvents)
            {
                if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(10)) != pdTRUE)
                {
                    log0("[AI] Event queue full");
                }
            }
            prevState = state;
            logf("[AI] State %s prob=%.3f score=%.3f night=%s",
                 state ? "CRY" : "CALM", prob, score, nightMode ? "ON" : "OFF");
        }
        filled = 0;
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(INFER_INTERVAL_S * 1000)));
    }
}

// ==== Task Sender ====
void taskSender(void *param)
{
    if (!qEvents)
    {
        vTaskDelete(nullptr);
        return;
    }
    CryEvent evt;
    for (;;)
    {
        if (xQueueReceive(qEvents, &evt, portMAX_DELAY) != pdTRUE)
            continue;
        // cần Wi-Fi sẵn sàng
        uint32_t waitStart = millis();
        while (!g_wifiConnected && millis() - waitStart < 15000)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        String json = "{";
        json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
        const char *evtName = evt.heartbeat ? "cry_heartbeat" : (evt.crying ? "cry_on" : "cry_off");
        json += "\"event\":\"" + String(evtName) + "\",";
        json += "\"prob\":" + String(evt.prob, 3) + ",";
        json += "\"score\":" + String(evt.score, 3) + ",";
        json += "\"lat\":" + String(evt.lat, 6) + ",";
        json += "\"lng\":" + String(evt.lon, 6) + ",";
        json += "\"gps_valid\":";
        json += (evt.gpsValid ? "true" : "false");
        json += ",";
        json += "\"duration_ms\":" + String(evt.durationMs) + ",";
        json += "\"ts\":" + String(evt.tsMs);
        json += "}";
        RestClient::postJSON(BACKEND_URL, json);
    }
}

// ==== Parser NMEA (GGA/RMC) ====
static double nmeaToDeg(const String &v, const String &dir)
{
    if (v.length() < 4)
        return 0;
    double raw = v.toDouble();
    int deg = int(raw / 100);
    double minutes = raw - deg * 100;
    double dec = deg + minutes / 60.0;
    if (dir == "S" || dir == "W")
        dec = -dec;
    return dec;
}

bool parseNMEA(const char *line, size_t len, GpsData &out)
{
    // token hóa nhẹ, tránh String để giảm phân mảnh heap
    constexpr size_t MAX_TOK = 20;
    const char *tokens[MAX_TOK] = {0};
    size_t tok_count = 0;
    // tạo bản sao tạm để strtok_r
    char buf[160];
    size_t copy_len = std::min(len, sizeof(buf) - 1);
    memcpy(buf, line, copy_len);
    buf[copy_len] = '\0';
    char *saveptr = nullptr;
    char *p = strtok_r(buf, ",", &saveptr);
    while (p && tok_count < MAX_TOK)
    {
        tokens[tok_count++] = p;
        p = strtok_r(nullptr, ",", &saveptr);
    }
    if (tok_count == 0 || !tokens[0])
        return false;

    auto endsWith = [](const char *s, const char *suffix)
    {
        size_t ls = strlen(s), lsf = strlen(suffix);
        if (ls < lsf)
            return false;
        return strncmp(s + ls - lsf, suffix, lsf) == 0;
    };

    if (endsWith(tokens[0], "GGA") && tok_count > 9)
    {
        out.lat = nmeaToDeg(String(tokens[2]), String(tokens[3]));
        out.lon = nmeaToDeg(String(tokens[4]), String(tokens[5]));
        out.fix = (String(tokens[6]).toInt() > 0);
        out.sats = String(tokens[7]).toInt();
        return true;
    }
    if (endsWith(tokens[0], "RMC") && tok_count > 11)
    {
        if (tokens[2][0] != 'A')
        {
            out.fix = false;
            return true;
        }
        out.lat = nmeaToDeg(String(tokens[3]), String(tokens[4]));
        out.lon = nmeaToDeg(String(tokens[5]), String(tokens[6]));
        out.speed = String(tokens[7]).toFloat() * 0.514444f; // knots -> m/s
        out.fix = true;
        return true;
    }
    return false;
}

// ==== Arduino setup/loop ====
void setup()
{
    Serial0.begin(115200);
    delay(200);
    log0("[BOOT] start app");
    gGpsMutex = xSemaphoreCreateMutex();

    setupWiFi();
    setupGPS();
    checkPsram();
    setupAudioCry(); // Tạm comment để khoanh vùng reboot
    setupWebServer();
    // Khởi tạo LED báo trạng thái (tắt sẵn)
    updateCryLed(false);
    pinMode(LED_NIGHT_PIN, OUTPUT);
    updateNightLed();
    updateWifiLed(false);

    // Luôn tạo speaker queue/task nếu có loa
#if USE_MAX98357A_SPK
    if (!qSpeaker)
    {
        qSpeaker = xQueueCreate(4, sizeof(SpeakerEvent));
        xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, &hSpeaker, 1);
    }
#endif

    xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 2, &hWifi, 0);
    xTaskCreatePinnedToCore(taskGps, "gps", 4096, nullptr, 3, &hGps, 1);
    xTaskCreatePinnedToCore(taskApp, "app", 4096, nullptr, 1, &hApp, 1);
    xTaskCreatePinnedToCore(taskWeb, "web", 4096, nullptr, 1, nullptr, 1);
    if (ENABLE_AUDIOCRY)
    {
        if (initPcmPool())
        {
            qEvents = xQueueCreate(6, sizeof(CryEvent));
            xTaskCreatePinnedToCore(taskMic, "mic", 3072, nullptr, 2, &hMic, 0);
            xTaskCreatePinnedToCore(taskInfer, "infer", 7168, nullptr, 1, &hInfer, 1);
            xTaskCreatePinnedToCore(taskSender, "sender", 3072, nullptr, 1, &hSender, 1);
        }
        else
        {
            log0("[Init] Failed to init PCM pool, AudioCry disabled");
        }
    }
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#include <Arduino.h>
// UPDATE: Đã kiểm tra runtime theo dự án test (WiFi/HTTP, MIC/LOA/GPS, Flash)
#include <WiFi.h>

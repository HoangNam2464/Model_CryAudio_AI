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
#include <Preferences.h>

HardwareSerial Serial0(0);

#include "Config.h"
#include "board_config.h"
#include "WifiConfig.h"
#include "wifi_service.h"
void log0(const String &msg);
void setStatusMessage(const char *msg);

#include <CryDetector.h>
#include <tflm_infer.h>
#include "audio_service/audio_service.h"
#include "backend_service.h"
#include "api_service.h"
#include "wifi_portal.h"
#include <time.h>

String currentTimestamp()
{
    time_t now;
    time(&now);

    struct tm *tm_info = localtime(&now);

    char buffer[32];
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", tm_info);

    return String(buffer);
}

static constexpr bool LOG_GPS_RAW = false;
static constexpr float CRY_THRESHOLD = 0.80f;
static constexpr bool ENABLE_AUDIOCRY = true;
static constexpr bool ENABLE_SPEAKER_FEEDBACK = false;

// ====== Wi-Fi cấu hình ======
static const uint32_t WIFI_BACKOFF_MIN_MS = 1000;
static const uint32_t WIFI_BACKOFF_MAX_MS = 30000;
static const uint32_t WIFI_AP_FALLBACK_MS = 20000;
static const char *SETUP_AP_SSID = "AudioCry-Setup";
static const char *SETUP_AP_PASS = "12345678";

// ====== I2S mic/loa (dùng nếu ENABLE_AUDIOCRY) ======
static const int MIC_SD = I2S_SD_PIN;
static const int MIC_WS = I2S_WS_PIN;
static const int MIC_SCK = I2S_SCK_PIN;
static const int SPK_DIN = I2S_SD_OUT_PIN;
static const int SPK_BCLK = SPK_I2S_BCLK_PIN;
static const int SPK_LRCLK = SPK_I2S_LRCK_PIN;

// ====== FreeRTOS handles ======
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
static WebServer g_webServer(80);
static bool g_psramOk = false;
static bool g_wifiConnected = false; // stub for legacy handlers
static bool g_apActive = false;      // stub for legacy handlers
static bool g_wifiReconnectRequested = false;
static uint32_t g_lastWifiLostMs = 0;

// ====== Cry event ======
struct CryEvent
{
    CryInfo cry;
    GpsInfo gps;
};

// ====== Audio / AI ======
static constexpr uint32_t PCM_POOL_BUFFERS = 2;
static constexpr size_t TARGET_SAMPLES = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S);
static int16_t *g_inferWindow = nullptr;
static uint32_t g_pcmBuffersAllocated = 0;
static bool nightMode = false;
static bool indoorMode = false;
static char g_profileName[16] = "balanced";
static Preferences g_appPrefs;
static double g_fixedLat = 0;
static double g_fixedLng = 0;
static float g_lastProb = 0.0f;
static float g_lastScore = 0.0f;
static bool g_isCrying = false;
static char g_lastEvent[16] = "boot";
static uint32_t g_lastEventTs = 0;
static bool lastWasCry = false;
static double g_lastLat = 0;
static double g_lastLng = 0;
static bool g_gpsValid = false;
char g_statusMessage[64] = "Booting";

static int readBatteryPercent()
{
    return 100;
}
void sendEventToBackend(bool cry, float prob, double lat, double lng, bool gpsValid)
{
    String mode = nightMode ? "NIGHT" : "DAY";
    int battery = readBatteryPercent();
    int deviceId = 1;

    String ts = currentTimestamp();

    api_send_event(
        cry,
        prob,
        mode,
        gpsValid,
        lat,
        lng,
        battery,
        ts,
        deviceId);
}

static DeviceStatus makeDeviceStatus()
{
    DeviceStatus st{};
    strncpy(st.mode, nightMode ? "night" : "day", sizeof(st.mode) - 1);
    st.mode[sizeof(st.mode) - 1] = '\0';
    st.battery_percent = readBatteryPercent();
    st.wifi_rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    st.uptime_ms = millis();
    WiFi.localIP().toString().toCharArray(st.device_ip, sizeof(st.device_ip));
    return st;
}
// ==== WiFi connected callback ====
void onWifiConnected()
{
    log0("[WiFi] ĐÃ KẾT NỐI WIFI!");
    pinMode(LED_WIFI_PIN, OUTPUT);
    digitalWrite(LED_WIFI_PIN, HIGH);
    playWifiSuccess();

    setStatusMessage("WiFi Connected");
}

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
static void app_state_set_mode(bool night, bool indoor)
{
    (void)night;
    (void)indoor;
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
    // LED trắng: bật khi ban ngày, tắt khi ban đêm
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
static constexpr DetectorProfile DAY_HIGH{
    0.65f, 0.17f, 0.8f, 1.8f, 1.6f, 1.3f};
static constexpr DetectorProfile NIGHT_HIGH{
    0.72f, 0.20f, 1.2f, 2.3f, 2.0f, 1.7f};
static constexpr DetectorProfile DAY_LOW_FALSE{
    0.82f, 0.25f, 1.6f, 3.0f, 2.5f, 2.2f};
static constexpr DetectorProfile NIGHT_LOW_FALSE{
    0.86f, 0.28f, 1.8f, 3.2f, 2.8f, 2.4f};

static const DetectorProfile &pickProfile(bool isNight)
{
    if (strncmp(g_profileName, "high_sensitivity", sizeof(g_profileName)) == 0)
        return isNight ? NIGHT_HIGH : DAY_HIGH;
    if (strncmp(g_profileName, "low_false_alarm", sizeof(g_profileName)) == 0)
        return isNight ? NIGHT_LOW_FALSE : DAY_LOW_FALSE;
    return isNight ? NIGHT_PROFILE : DAY_PROFILE;
}

static void saveProfileToNvs(const char *name)
{
    if (!name)
        return;
    if (!g_appPrefs.begin("app_cfg", false))
        return;
    g_appPrefs.putString("profile", name);
    g_appPrefs.end();
}

static void loadProfileFromNvs()
{
    if (!g_appPrefs.begin("app_cfg", false))
        return;
    String p = g_appPrefs.getString("profile", "balanced");
    if (p.length() >= (int)sizeof(g_profileName))
        p = p.substring(0, sizeof(g_profileName) - 1);
    strncpy(g_profileName, p.c_str(), sizeof(g_profileName) - 1);
    g_profileName[sizeof(g_profileName) - 1] = '\0';
    g_appPrefs.end();
}

static void saveIndoorToNvs()
{
    if (!g_appPrefs.begin("app_cfg", false))
        return;
    g_appPrefs.putBool("indoor", indoorMode);
    g_appPrefs.putDouble("fix_lat", g_fixedLat);
    g_appPrefs.putDouble("fix_lng", g_fixedLng);
    g_appPrefs.end();
}

static void loadIndoorFromNvs()
{
    if (!g_appPrefs.begin("app_cfg", false))
        return;
    indoorMode = g_appPrefs.getBool("indoor", false);
    g_fixedLat = g_appPrefs.getDouble("fix_lat", 0);
    g_fixedLng = g_appPrefs.getDouble("fix_lng", 0);
    g_appPrefs.end();
    app_state_set_mode(nightMode, indoorMode);
}

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
void setupGPS();
void setupAudioCry();
void setupWebServer();
void checkPsram();
void taskGps(void *param);
void taskApp(void *param);
void taskMic(void *param);
void taskInfer(void *param);
void taskSpeaker(void *param);
void taskSender(void *param);
void taskWeb(void *param);
bool parseNMEA(const char *line, size_t len, GpsData &out);
void applyDetectorProfile();
static void logSpiffsAudioFiles();
static void audioSelfTest();

// ==== CryDetector profile ====
void applyDetectorProfile()
{
    const DetectorProfile &p = pickProfile(nightMode);
    detector.configure(p.onTh, p.offTh, p.stableOn, p.stableOff, p.minOn, p.minOff);
}

static void setDetectorProfileName(const char *name)
{
    if (!name)
        return;
    strncpy(g_profileName, name, sizeof(g_profileName) - 1);
    g_profileName[sizeof(g_profileName) - 1] = '\0';
    saveProfileToNvs(g_profileName);
    applyDetectorProfile();
}

static void logSpiffsAudioFiles()
{
    File root = SPIFFS.open("/audio");
    if (!root || !root.isDirectory())
    {
        log0("[AUDIO] Folder /audio not found in SPIFFS");
        return;
    }
    File f = root.openNextFile();
    int count = 0;
    while (f)
    {
        count++;
        logf("[AUDIO] file: %s (%u bytes)", f.name(), (unsigned)f.size());
        f = root.openNextFile();
    }
    if (count == 0)
    {
        log0("[AUDIO] No files under /audio in SPIFFS");
    }
}

// Phát thử PCM để kiểm tra loa/I2S ngay khi boot
static void audioSelfTest()
{
    log0("[AUDIO] Self-test: playing calm alert (PCM preferred)");
    playCalmAlert();
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
        setStatusMessage("PSRAM không thấy, AI có thể tắt.");
    }
}

// ==== Web server (status + Wi-Fi config) ====
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

static bool loadTestAudio(int16_t *dst, size_t maxSamples, size_t &outSamples)
{
    outSamples = 0;
    File f = SPIFFS.open("/audio/test_cry.wav", "r");
    if (!f)
        return false;
    if (f.size() < 44)
        return false;
    f.seek(44); // skip WAV header (assume PCM16 mono)
    size_t toRead = maxSamples * sizeof(int16_t);
    int bytes = f.read((uint8_t *)dst, toRead);
    if (bytes <= 0)
        return false;
    outSamples = bytes / sizeof(int16_t);
    return outSamples > 0;
}

static void handleSelfTest()
{
    static int16_t testBuf[TARGET_SAMPLES];
    size_t got = 0;
    if (!loadTestAudio(testBuf, TARGET_SAMPLES, got))
    {
        g_webServer.send(404, "application/json", "{\"ok\":false,\"error\":\"test_cry.wav missing or invalid\"}");
        return;
    }
    float prob = tflm_infer_prob(testBuf, got);
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"prob\":%.3f,\"samples\":%u}", prob, (unsigned)got);
    g_webServer.send(200, "application/json", resp);
}

static void handleDashboard()
{
    String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:1.5rem;} .card{border:1px solid #ccc;padding:1rem;border-radius:8px;margin-bottom:1rem;} h2{margin:0 0 .5rem 0;}</style></head><body>";
    html += "<h2>AudioCry Dashboard</h2>";
    html += "<div class='card'><strong>WiFi</strong><br>SSID: " + String(WiFi.SSID()) + "<br>RSSI: " + String(WiFi.RSSI()) + "<br>IP: " + WiFi.localIP().toString() + "</div>";
    html += "<div class='card'><strong>AI</strong><br>Prob: " + String(g_lastProb, 3) + "<br>State: " + String(g_isCrying ? "CRYING" : "CALM") + "</div>";
    html += "<div class='card'><strong>GPS</strong><br>Fix: " + String(g_gpsValid ? "YES" : "NO") + " sats=" + String(g_gps.sats) + "<br>Lat/Lng: " + String(g_lastLat, 6) + ", " + String(g_lastLng, 6) + "</div>";
    html += "<div class='card'><strong>Mode</strong><br>Day/Night: " + String(nightMode ? "NIGHT" : "DAY") + "<br>Indoor: " + String(indoorMode ? "ON" : "OFF") + "<br>Status: " + String(g_statusMessage) + "</div>";
    html += "</body></html>";
    g_webServer.send(200, "text/html", html);
}

static void handleTestSpeaker()
{
    playTestTone();
    g_webServer.send(200, "text/plain", "OK speaker");
}

static void handleTestAi() { handleSelfTest(); }

static void handleSettings()
{
    if (g_webServer.method() == HTTP_POST)
    {
        indoorMode = g_webServer.arg("indoor") == "1";
        g_fixedLat = g_webServer.arg("lat").toDouble();
        g_fixedLng = g_webServer.arg("lng").toDouble();
        String prof = g_webServer.arg("profile");
        if (prof.length())
            setDetectorProfileName(prof.c_str());
        saveIndoorToNvs();
        app_state_set_mode(nightMode, indoorMode);
    }
    String html = "<!doctype html><html><body><h3>Settings</h3><form method='POST'>";
    html += "Indoor mode: <input type='checkbox' name='indoor' value='1' " + String(indoorMode ? "checked" : "") + "><br>";
    html += "Fixed lat: <input name='lat' value='" + String(g_fixedLat, 6) + "'><br>";
    html += "Fixed lng: <input name='lng' value='" + String(g_fixedLng, 6) + "'><br>";
    html += "Profile: <select name='profile'>";
    html += "<option value='balanced' " + String(strcmp(g_profileName, "balanced") == 0 ? "selected" : "") + ">balanced</option>";
    html += "<option value='high_sensitivity' " + String(strcmp(g_profileName, "high_sensitivity") == 0 ? "selected" : "") + ">high_sensitivity</option>";
    html += "<option value='low_false_alarm' " + String(strcmp(g_profileName, "low_false_alarm") == 0 ? "selected" : "") + ">low_false_alarm</option>";
    html += "</select><br><button type='submit'>Save</button></form></body></html>";
    g_webServer.send(200, "text/html", html);
}

void setupWebServer()
{
    g_webServer.on("/", handleRoot);
    g_webServer.on("/status", handleStatusJson);
    g_webServer.on("/dashboard", HTTP_GET, handleDashboard);
    g_webServer.on("/self-test", HTTP_GET, handleSelfTest);
    g_webServer.on("/test/ai", HTTP_GET, handleTestAi);
    g_webServer.on("/test/speaker", HTTP_GET, handleTestSpeaker);
    g_webServer.on("/settings", HTTP_GET, handleSettings);
    g_webServer.on("/settings", HTTP_POST, handleSettings);
    wifi_portal_register(g_webServer);
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

    const bool sharedPort = (MIC_I2S_PORT == SPK_I2S_PORT);
    logf("[AUDIO] Config MIC port=%d WS=%d BCLK=%d SD=%d", MIC_I2S_PORT, MIC_WS, MIC_SCK, MIC_SD);
    logf("[AUDIO] Config SPK port=%d BCLK=%d LRCK=%d DIN=%d", SPK_I2S_PORT, SPK_BCLK, SPK_LRCLK, SPK_DIN);

    if (sharedPort)
    {
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
    }
    else
    {
        if (needMic)
        {
            i2s_config_t rxCfg = {};
            rxCfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
            rxCfg.sample_rate = I2S_SAMPLE_RATE;
            rxCfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
            rxCfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
            rxCfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
            rxCfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
            rxCfg.dma_buf_count = 8;
            rxCfg.dma_buf_len = I2S_READ_LEN;
            rxCfg.use_apll = false;
            rxCfg.tx_desc_auto_clear = false;
            rxCfg.fixed_mclk = 0;

            i2s_pin_config_t rxPins = {};
            rxPins.bck_io_num = MIC_SCK;
            rxPins.ws_io_num = MIC_WS;
            rxPins.data_out_num = I2S_PIN_NO_CHANGE;
            rxPins.data_in_num = MIC_SD;

            i2s_driver_install(MIC_I2S_PORT, &rxCfg, 0, nullptr);
            i2s_set_pin(MIC_I2S_PORT, &rxPins);
            i2s_set_clk(MIC_I2S_PORT, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
        }

        if (needSpeaker)
        {
            i2s_config_t txCfg = {};
            txCfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
            txCfg.sample_rate = I2S_SAMPLE_RATE;
            txCfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
            txCfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
            txCfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
            txCfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
            txCfg.dma_buf_count = 8;
            txCfg.dma_buf_len = I2S_READ_LEN;
            txCfg.use_apll = false;
            txCfg.tx_desc_auto_clear = true;
            txCfg.fixed_mclk = 0;

            i2s_pin_config_t txPins = {};
            txPins.bck_io_num = SPK_BCLK;
            txPins.ws_io_num = SPK_LRCLK;
            txPins.data_out_num = SPK_DIN;
            txPins.data_in_num = I2S_PIN_NO_CHANGE;

            i2s_driver_install(SPK_I2S_PORT, &txCfg, 0, nullptr);
            i2s_set_pin(SPK_I2S_PORT, &txPins);
            i2s_set_clk(SPK_I2S_PORT, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
        }
    }

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
// ==== Task GPS ====
void taskGps(void *param)
{
    log0("[GPS] task started");
    char lineBuf[160];
    size_t idx = 0;
    uint32_t lastRx = millis();
    uint32_t lastAnyNmeaMs = millis();
    uint32_t lastSatsZeroMs = millis();
    int raw_logged = 0;
    static bool loggedNoData = false;
    static bool loggedNoNmea = false;
    static bool loggedZeroSat = false;
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
                        loggedNoNmea = false;
                        loggedNoData = false;
                        if (tmp.sats == 0)
                        {
                            if ((millis() - lastSatsZeroMs > 30000) && !loggedZeroSat)
                            {
                                log0("[GPS] Sats=0 >30s, kiểm tra anten/vị trí...");
                                lastSatsZeroMs = millis();
                                loggedZeroSat = true;
                            }
                        }
                        else
                        {
                            loggedZeroSat = false;
                        }
                    }
                    else if (LOG_GPS_RAW && raw_logged < 5)
                    {
                        lineBuf[(idx < sizeof(lineBuf)) ? idx : (sizeof(lineBuf) - 1)] = '\0';
                        logf("[GPS][RAW] %s", lineBuf);
                        raw_logged++;
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
        if (millis() - lastRx > 5000 && !loggedNoData)
        {
            log0("[GPS] No data >5s");
            loggedNoData = true;
        }
        // mute noisy warning about missing NMEA
        if (millis() - lastAnyNmeaMs > 30000)
        {
            loggedNoNmea = true;
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
    bool lastFixState = false;
    bool loggedNoFix = false;

    for (;;)
    {
        // Giảm spam log GPS: chỉ log khi đổi trạng thái hoặc mỗi 10 giây
        uint32_t now = millis();
        const uint32_t LOG_INTERVAL = 10000;

        if (now - lastLog > LOG_INTERVAL || lastFixState != g_gps.fix)
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

            if (snapshot.fix)
            {
                logf("[GPS] FIX  | Lat=%.6f Lon=%.6f  V=%.2f  Sat=%u",
                     snapshot.lat, snapshot.lon, snapshot.speed, snapshot.sats);

                loggedNoFix = false;
            }
            else
            {
                if (!loggedNoFix || lastFixState != snapshot.fix)
                {
                    logf("[GPS] NOFIX | Sat=%u | Đang tìm tín hiệu...", snapshot.sats);
                    loggedNoFix = true;
                }
            }

            lastFixState = snapshot.fix;
            lastLog = now;
        }

        // ==== Nút gạt chế độ day/night ====
        bool btn = (digitalRead(MODE_BUTTON_PIN) == LOW);
        uint32_t nowMs = millis();

        if (btn && !lastBtn && (nowMs - lastChange) > 50)
        {
            lastChange = nowMs;

            nightMode = !nightMode;
            applyDetectorProfile();

            if (qSpeaker)
            {
                SpeakerEvent ev = nightMode ? SpeakerEvent::EVENT_MODE_NIGHT
                                            : SpeakerEvent::EVENT_MODE_DAY;
                xQueueSend(qSpeaker, &ev, 0);
            }

            updateNightLed();

            // Nháy LED báo chuyển mode
            digitalWrite(LED_WIFI_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(LED_WIFI_PIN, LOW);

            // Khôi phục LED WiFi theo trạng thái kết nối
            wifi_update_led();

            logf("[MODE] nightMode=%s", nightMode ? "ON" : "OFF");
            app_state_set_mode(nightMode, indoorMode);
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

    const size_t bytes_per_block = I2S_READ_LEN * sizeof(int16_t);
    const TickType_t timeout = pdMS_TO_TICKS(80); // đủ để fill 1024 mẫu

    while (true)
    {
        esp_task_wdt_reset();

        // Lấy block từ pool
        int16_t *block = acquirePcmBlock(pdMS_TO_TICKS(50));
        if (!block)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // Đọc I2S
        esp_err_t err = i2s_read(
            MIC_I2S_PORT,
            block,
            bytes_per_block,
            &bytes_read,
            timeout);

        // ---------- Kiểm tra lỗi ----------
        if (err != ESP_OK)
        {
            logf("[MIC] I2S ERROR: %d", err);
            releasePcmBlock(block);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ---------- Không có đủ dữ liệu ----------
        if (bytes_read == 0)
        {
            static int zero_cnt = 0;
            if (++zero_cnt % 200 == 0)
                log0("[MIC] Timeout (bytes_read=0) – KHÔNG phải lỗi mic");

            releasePcmBlock(block);
            continue;
        }

        // ---------- PARTIAL → đọc đủ bằng vòng lặp bổ sung ----------
        if (bytes_read < bytes_per_block)
        {
            size_t remain = bytes_per_block - bytes_read;
            uint8_t *dst = (uint8_t *)block + bytes_read;

            size_t br2 = 0;
            i2s_read(MIC_I2S_PORT, dst, remain, &br2, timeout);
            bytes_read += br2;

            if (bytes_read < bytes_per_block)
            {
                logf("[MIC] PARTIAL: only %u/%u bytes", bytes_read, bytes_per_block);
                releasePcmBlock(block);
                continue;
            }
        }

        blocksOk++;
        if (blocksOk % 200 == 0)
            logf("[MIC] OK blocks=%u", blocksOk);
        // ---------- Gửi block vào queue PCM ----------
        if (xQueueSend(qPcm, &block, 0) != pdTRUE)
        {
            releasePcmBlock(block);
        }

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
    // Deprecated: use audio_service playback functions instead.
}

void taskSpeaker(void *param)
{
    SpeakerEvent ev = SpeakerEvent::EVENT_CALM_DAY;
    for (;;)
    {
        if (xQueueReceive(qSpeaker, &ev, portMAX_DELAY) == pdTRUE)
        {
            logf("[AUDIO] Speaker event=%d night=%s", static_cast<int>(ev), nightMode ? "ON" : "OFF");
            switch (ev)
            {
            case SpeakerEvent::EVENT_MODE_DAY:
                playNightModeOff();
                break;
            case SpeakerEvent::EVENT_MODE_NIGHT:
                playNightModeOn();
                break;
            case SpeakerEvent::EVENT_CRY_DAY:
                playCryAlert();
                break;
            case SpeakerEvent::EVENT_CALM_DAY:
                playCalmAlert();
                break;
            case SpeakerEvent::EVENT_CRY_NIGHT:
                playCryAlert();
                break;
            case SpeakerEvent::EVENT_CALM_NIGHT:
                playCalmAlert();
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
    bool prevState = false; // false = CALM, true = CRY

    for (;;)
    {
        // Gom đủ TARGET_SAMPLES mẫu PCM cho 1 cửa sổ infer
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

        // Chạy AI
        float prob = tflm_infer_prob(g_inferWindow, TARGET_SAMPLES);
        bool state = detector.update(prob);
        float score = detector.score();

        g_lastProb = prob;
        g_lastScore = score;
        g_isCrying = state;

        // 👉 Chỉ xử lý khi TRẠNG THÁI THAY ĐỔI (CALM ↔ CRY)
        if (state != prevState)
        {
            // LED báo trạng thái
            updateCryLed(state);

            // ===== Loa: chỉ phát 1 lần khi đổi trạng thái =====
            if (qSpeaker)
            {
                SpeakerEvent ev;
                if (nightMode)
                    ev = state ? SpeakerEvent::EVENT_CRY_NIGHT : SpeakerEvent::EVENT_CALM_NIGHT;
                else
                    ev = state ? SpeakerEvent::EVENT_CRY_DAY : SpeakerEvent::EVENT_CALM_DAY;

                // Không spam: mỗi lần đổi trạng thái chỉ gửi 1 event
                xQueueSend(qSpeaker, &ev, 0);
            }

            // ===== GPS snapshot tại thời điểm đổi trạng thái =====
            GpsData snapshot{};
            if (xSemaphoreTake(gGpsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                snapshot = g_gps;
                xSemaphoreGive(gGpsMutex);
            }

            if (indoorMode)
            {
                snapshot.fix = false;
                snapshot.lat = g_fixedLat;
                snapshot.lon = g_fixedLng;
                snapshot.sats = 0;
            }

            g_lastLat = snapshot.lat;
            g_lastLng = snapshot.lon;
            g_gpsValid = snapshot.fix;
            strncpy(g_lastEvent, state ? "cry_on" : "cry_off", sizeof(g_lastEvent) - 1);
            g_lastEvent[sizeof(g_lastEvent) - 1] = '\0';

            uint32_t nowMs = millis();
            g_lastEventTs = nowMs;

            // ===== Backend event: CHỈ gửi khi CALM -> CRY =====
            if (state         // đang CRY
                && !prevState // trước đó là CALM
                && prob >= CRY_THRESHOLD && qEvents)
            {
                CryEvent evt{};
                evt.cry.detected = true;
                evt.cry.prob = prob;
                strncpy(evt.cry.state, "CRYING", sizeof(evt.cry.state) - 1);
                evt.cry.state[sizeof(evt.cry.state) - 1] = '\0';
                evt.cry.threshold = CRY_THRESHOLD;
                evt.cry.duration_ms = static_cast<uint32_t>(INFER_INTERVAL_S * 1000);
                evt.cry.timestamp_ms = nowMs;

                evt.gps.valid = snapshot.fix;
                evt.gps.lat = snapshot.lat;
                evt.gps.lng = snapshot.lon;
                evt.gps.sats = snapshot.sats;

                if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(10)) != pdTRUE)
                {
                    log0("[AI] Event queue full");
                }

                logf("[AI] Baby cry detected, prob=%.3f, mode=%s",
                     prob, nightMode ? "night" : "day");
            }

            prevState = state;
            if (!state) // CALM thì reset cờ gửi
                lastWasCry = false;

            logf("[AI] prob_cry=%.3f state=%s score=%.3f night=%s",
                 prob, state ? "CRY" : "CALM", score, nightMode ? "ON" : "OFF");
        }

        // Chuẩn bị cho cửa sổ kế tiếp
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
    static uint32_t lastSentMs = 0;

    for (;;)
    {
        // Block tới khi nhận được sự kiện từ AI
        if (xQueueReceive(qEvents, &evt, portMAX_DELAY) != pdTRUE)
            continue;
        if (!evt.cry.detected)
            continue;

        if (lastWasCry)
        {
            continue;
        }

        lastWasCry = true;
        lastSentMs = millis();
        wifi_ensure_connected(15000);

        // ---- Gửi về backend ----
        sendEventToBackend(
            evt.cry.detected,
            evt.cry.prob,
            evt.gps.lat,
            evt.gps.lng,
            evt.gps.valid);

        logf("[SEND] Event sent: prob=%.3f lat=%.6f lng=%.6f valid=%d",
             evt.cry.prob, evt.gps.lat, evt.gps.lng, evt.gps.valid);
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

void setup()
{
    Serial0.begin(115200);
    delay(200);
    log0("[BOOT] start app");
    gGpsMutex = xSemaphoreCreateMutex();
    loadProfileFromNvs();
    loadIndoorFromNvs();
    applyDetectorProfile();
    app_state_set_mode(nightMode, indoorMode);

    wifi_service_init();
    wifi_service_start();

    setupGPS();
    checkPsram();
    setupAudioCry();

    // SPIFFS mount...
    if (!audioInitFS())
    {
        log0("[Init] SPIFFS mount failed, speaker playback will not work");
    }
    else if (!SPIFFS.exists(AUDIO_ADPCM_NIGHT_ON) &&
             !SPIFFS.exists(AUDIO_PCM_NIGHT_ON))
    {
        log0("[Init] Audio files missing in SPIFFS (/audio/*.wav). Upload data folder before testing speaker.");
    }
    else
    {
        logSpiffsAudioFiles();
    }

    setupWebServer();
    updateCryLed(false);
    pinMode(LED_NIGHT_PIN, OUTPUT);
    updateNightLed();
    wifi_update_led();

    delay(6000); // chờ WiFi kết nối

    int testDeviceId = 1; // ID thiết bị trong bảng thiet_bis
    String ts = currentTimestamp();

    Serial0.println("[TEST] Sending test cry event...");

    api_send_event(
        true,  // isCrying
        0.92f, // prob
        "DAY", // mode
        false, // gpsValid
        0.0,   // lat
        0.0,   // lng
        100,   // battery
        ts,    // timestamp
        testDeviceId);

    Serial0.println("[TEST] Done sending");
    // ====================================================================

#if USE_MAX98357A_SPK
    if (!qSpeaker)
    {
        qSpeaker = xQueueCreate(4, sizeof(SpeakerEvent));
        xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, &hSpeaker, 1);
    }
#endif

    // Tạo các task khác
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

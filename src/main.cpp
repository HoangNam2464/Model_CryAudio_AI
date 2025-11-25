// ESP32 DevKit / ESP32-S3
// Wi-Fi + GPS ổn định, kèm AudioCry (mic/I2S + TFLM + speaker + API) tùy chọn.
// Lưu ý: AudioCry cần nhiều RAM, khuyến nghị ESP32-S3/WROVER có PSRAM. DevKit không PSRAM có thể thiếu heap khi bật AI.

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

#include "Config.h"
#include "board_config.h"
#include <CryDetector.h>
#include <RestClient.h>
#include <tflm_infer.h>

// ====== Cờ bật/tắt AudioCry ======
// Để test Wi-Fi + GPS trên DevKit không PSRAM, tắt AudioCry nếu thiếu RAM.
static constexpr bool ENABLE_AUDIOCRY = false;
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

// Speaker command
enum class SpeakCmd : uint8_t
{
    Cry,
    Calm,
    NightOn,
    NightOff,
    Ting,
    NightModeOnVoice,
    NightModeOffVoice,
    CryDayVoice,
    CalmDayVoice
};

// Forward declarations
void setupWiFi();
void setupGPS();
void setupAudioCry();
void setupSpeakerFeedback();
void taskWifi(void *param);
void taskGps(void *param);
void taskApp(void *param);
void taskMic(void *param);
void taskInfer(void *param);
void taskSpeaker(void *param);
void taskSender(void *param);
bool parseNMEA(const char *line, size_t len, GpsData &out);
void startSetupAP();
void stopSetupAP();
void applyDetectorProfile();

// ==== Wi-Fi event callback ====
void handleWiFiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("[WiFi] Connected to AP");
        updateWifiLed(true);
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        g_wifiConnected = true;
        stopSetupAP();
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        updateWifiLed(true);
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        g_wifiConnected = false;
        g_lastWifiLostMs = millis();
        Serial.println("[WiFi] Lost");
        updateWifiLed(false);
        break;
    default:
        break;
    }
}

// ==== Setup Wi-Fi ====
void setupWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(handleWiFiEvent);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    g_lastWifiLostMs = millis();
}

// ==== AP fallback ====
void startSetupAP()
{
    if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);
    Serial.printf("[WiFi] AP fallback: %s / %s\n", SETUP_AP_SSID, SETUP_AP_PASS);
}

void stopSetupAP()
{
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
}

// ==== Setup GPS ====
void setupGPS()
{
    Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] Serial2 started");
}

// ==== Setup AudioCry (I2S) ====
void setupAudioCry()
{
    bool needSpeaker = ENABLE_AUDIOCRY || ENABLE_SPEAKER_FEEDBACK;
    if (!needSpeaker && !ENABLE_AUDIOCRY)
        return;

    i2s_config_t cfg = {};
    cfg.mode = I2S_MODE_MASTER;
    if (ENABLE_AUDIOCRY)
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_RX | I2S_MODE_TX);
    else if (needSpeaker)
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX);
    cfg.sample_rate = I2S_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = I2S_READ_LEN;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = MIC_SCK;
    pins.ws_io_num = MIC_WS;
    pins.data_out_num = SPK_DIN;
    pins.data_in_num = ENABLE_AUDIOCRY ? MIC_SD : -1;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_set_clk(I2S_NUM_0, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

    // alloc infer window (chỉ khi chạy AI)
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

// ==== CryDetector profile ====
void applyDetectorProfile()
{
    const DetectorProfile &p = nightMode ? NIGHT_PROFILE : DAY_PROFILE;
    detector.configure(p.onTh, p.offTh, p.stableOn, p.stableOff, p.minOn, p.minOff);
}

// ==== Task Wi-Fi ====
void taskWifi(void *param)
{
    uint32_t backoff = WIFI_BACKOFF_MIN_MS;
    Serial.println("[WiFiTask] start");
    for (;;)
    {
        if (!g_wifiConnected)
        {
            // Tạm tắt AP fallback để tránh xung đột mode; chỉ thử STA trước
            if (WiFi.getMode() == WIFI_AP)
            {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
            }

            Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            uint32_t t0 = millis();
            while (!g_wifiConnected && (millis() - t0) < backoff)
            {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (g_wifiConnected)
            {
                Serial.println("[WiFi] Connected");
                backoff = WIFI_BACKOFF_MIN_MS;
            }
            else
            {
                Serial.printf("[WiFi] Retry in %u ms\n", backoff);
                backoff = std::min(backoff * 2, WIFI_BACKOFF_MAX_MS);
            }
        }
        static uint32_t lastLog = 0;
        uint32_t now = millis();
        if (g_wifiConnected && now - lastLog > 5000)
        {
            Serial.println("[WiFi] Link healthy");
            lastLog = now;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ==== Task GPS ====
void taskGps(void *param)
{
    Serial.println("[GPS] task started");
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
                                Serial.println("[GPS] Sats=0 >30s, kiểm tra anten/vị trí");
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
            Serial.println("[GPS] No data >5s");
            lastRx = millis();
        }
        if (millis() - lastAnyNmeaMs > 30000)
        {
            Serial.println("[GPS] Dữ liệu bất thường, không thấy GGA/RMC >30s. Kiểm tra anten/chân RX/TX.");
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
            Serial.printf("[GPS] Lat: %.6f, Lon: %.6f, Speed: %.2f, Sat: %u, Fix: %s\n",
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
            if (ENABLE_AUDIOCRY)
            {
                SpeakCmd cmd = nightMode ? SpeakCmd::NightOn : SpeakCmd::NightOff;
                if (qSpeaker)
                    xQueueSend(qSpeaker, &cmd, 0);
            }
            else if (ENABLE_SPEAKER_FEEDBACK)
            {
#if USE_MAX98357A_SPK
                if (qSpeaker)
                {
                    SpeakCmd cmd = nightMode ? SpeakCmd::NightModeOnVoice : SpeakCmd::NightModeOffVoice;
                    xQueueSend(qSpeaker, &cmd, 0);
                }
#endif
            }
            updateNightLed();
            // Nhay LED Wi-Fi (LED xanh duong tren board) de bao doi che do
            digitalWrite(LED_WIFI_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(LED_WIFI_PIN, LOW);
            // Khoi phuc trang thai LED Wi-Fi theo ket noi hien tai
            updateWifiLed(g_wifiConnected);
            Serial.printf("[MODE] nightMode=%s\n", nightMode ? "ON" : "OFF");
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
        esp_err_t err = i2s_read(I2S_NUM_0,
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
static void speaker_write(const int16_t *data, size_t samples)
{
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);
    size_t bytesRemaining = samples * sizeof(int16_t);
    while (bytesRemaining > 0)
    {
        size_t written = 0;
        i2s_write(I2S_NUM_0, ptr, bytesRemaining, &written, pdMS_TO_TICKS(200));
        if (written == 0)
            break;
        ptr += written;
        bytesRemaining -= written;
    }
}

static void speaker_play_cmd(SpeakCmd cmd)
{
    // Placeholder bằng các pattern beep khác nhau cho từng thông báo
    auto play_beep = [](float freq, float ms, float volume = 12000.0f) {
        const size_t N = static_cast<size_t>(I2S_SAMPLE_RATE * (ms / 1000.0f));
        static std::vector<int16_t> buf;
        buf.resize(N);
        for (size_t i = 0; i < N; ++i)
        {
            float env = sinf(3.14159f * i / N);
            float s = sinf(2.0f * 3.14159f * freq * i / I2S_SAMPLE_RATE) * env;
            buf[i] = static_cast<int16_t>(s * volume);
        }
        speaker_write(buf.data(), N);
    };

    switch (cmd)
    {
    case SpeakCmd::NightOn:
    case SpeakCmd::NightModeOnVoice:
        // "Đã bật chế độ ban đêm" -> 2 beep ngắn
        play_beep(600.0f, 150);
        vTaskDelay(pdMS_TO_TICKS(50));
        play_beep(700.0f, 200);
        break;
    case SpeakCmd::NightOff:
    case SpeakCmd::NightModeOffVoice:
        // "Đã bật chế độ ban ngày" -> 1 beep dài
        play_beep(800.0f, 300);
        break;
    case SpeakCmd::Cry:
        // Day: "Em bé đang khóc – hãy kiểm tra" -> 3 beep nhanh
        play_beep(550.0f, 180);
        vTaskDelay(pdMS_TO_TICKS(60));
        play_beep(550.0f, 180);
        vTaskDelay(pdMS_TO_TICKS(60));
        play_beep(650.0f, 220);
        break;
    case SpeakCmd::Calm:
        // Day: "Bé đã yên" -> beep mềm
        play_beep(450.0f, 200, 9000.0f);
        break;
    case SpeakCmd::Ting:
        // Night: ting ngắn
        play_beep(900.0f, 120);
        break;
    default:
        break;
    }
}

void taskSpeaker(void *param)
{
    SpeakCmd cmd = SpeakCmd::Calm;
    for (;;)
    {
        if (xQueueReceive(qSpeaker, &cmd, portMAX_DELAY) == pdTRUE)
        {
            speaker_play_cmd(cmd);
        }
    }
}

// ==== Task Infer ====
void taskInfer(void *param)
{
    if (!g_inferWindow)
    {
        Serial.println("[AI] No infer buffer");
        vTaskDelete(nullptr);
    }
    int retry = 0;
    const int kMaxRetry = 6;
    while (!tflm_begin())
    {
        retry++;
        Serial.println("[AI] Failed to init TFLM, retrying...");
        if (retry >= kMaxRetry)
        {
            Serial.println("[AI] Disable AI (init failed / low RAM). Dùng S3 PSRAM hoặc giảm arena.");
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
        if (state != prevState)
        {
            updateCryLed(state);
            if (ENABLE_AUDIOCRY && qSpeaker)
            {
                SpeakCmd cmd = nightMode ? SpeakCmd::Ting : (state ? SpeakCmd::Cry : SpeakCmd::Calm);
                xQueueSend(qSpeaker, &cmd, 0);
            }
            CryEvent evt{};
            evt.crying = state;
            evt.heartbeat = false;
            evt.prob = prob;
            evt.score = score;
            evt.tsMs = millis();
            if (qEvents)
            {
                if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(10)) != pdTRUE)
                {
                    Serial.println("[AI] Event queue full");
                }
            }
            prevState = state;
            Serial.printf("[AI] State %s prob=%.3f score=%.3f night=%s\n",
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

    auto endsWith = [](const char *s, const char *suffix) {
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
    Serial.begin(115200);
    delay(200);
    gGpsMutex = xSemaphoreCreateMutex();

    setupWiFi();
    setupGPS();
    setupAudioCry();
    // Khởi tạo LED báo trạng thái (tắt sẵn)
    updateCryLed(false);
    pinMode(LED_NIGHT_PIN, OUTPUT);
    updateNightLed();
    updateWifiLed(false);

    xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 2, &hWifi, 0);
    xTaskCreatePinnedToCore(taskGps, "gps", 4096, nullptr, 3, &hGps, 1);
    xTaskCreatePinnedToCore(taskApp, "app", 4096, nullptr, 1, &hApp, 1);
    if (ENABLE_AUDIOCRY)
    {
        if (initPcmPool())
        {
            qEvents = xQueueCreate(6, sizeof(CryEvent));
#if USE_MAX98357A_SPK
            qSpeaker = xQueueCreate(4, sizeof(SpeakCmd));
#endif
            xTaskCreatePinnedToCore(taskMic, "mic", 3072, nullptr, 2, &hMic, 0);
            xTaskCreatePinnedToCore(taskInfer, "infer", 7168, nullptr, 1, &hInfer, 1);
            xTaskCreatePinnedToCore(taskSender, "sender", 3072, nullptr, 1, &hSender, 1);
#if USE_MAX98357A_SPK
            xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, &hSpeaker, 1);
#endif
        }
        else
        {
            Serial.println("[Init] Failed to init PCM pool, AudioCry disabled");
        }
    }
    else if (ENABLE_SPEAKER_FEEDBACK)
    {
#if USE_MAX98357A_SPK
        qSpeaker = xQueueCreate(2, sizeof(SpeakCmd));
        xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, &hSpeaker, 1);
#endif
    }
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ================= HƯỚNG DẪN =================
1) Bật/tắt AudioCry:
   - Đặt ENABLE_AUDIOCRY = false nếu chạy trên DevKit không PSRAM (tránh thiếu RAM).
   - Đặt ENABLE_AUDIOCRY = true trên ESP32-S3/WROVER có PSRAM; bổ sung pipeline AI/loa nếu cần.

2) Tối ưu RAM:
   - Giảm PCM_POOL_BUFFERS nếu cần.
   - Giảm stack các task mic/infer/sender nếu thấy heap thấp.
   - Chuyển arena TFLM và buffer lên PSRAM (đã dùng heap_caps_malloc với MALLOC_CAP_SPIRAM).

3) Khi nào cần chuyển sang ESP32-S3/WROVER:
   - Khi bật AI TFLM, model lớn (>150 KB) sẽ cần PSRAM cho tensor arena và buffer mel.
   - Khi chạy đồng thời Wi-Fi + GPS + AI + loa mà DevKit báo thiếu heap hoặc reset.

4) Pin mẫu:
   - GPS (DevKit): RX=16, TX=17
   - GPS (S3): RX=43, TX=44
   - I2S mic: SD=32, WS=25, SCK=26
   - I2S loa: DIN=33, BCLK=14, LRCLK=27
*/

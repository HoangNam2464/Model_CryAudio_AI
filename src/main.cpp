#include <Arduino.h>
#include <WiFi.h>
#include "driver/i2s.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/event_groups.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "WifiConfig.h"
#include "api_server.h"
#include <CryDetector.h>
#include <RestClient.h>
#include <gps.h>
#include <tflm_infer.h>
#include <driver/gpio.h>
#if USE_MAX98357A_SPK
#include "SpeechSamples.h"
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

enum LogVerbosity {
    LOG_ERROR_LEVEL = 0,
    LOG_WARN_LEVEL  = 1,
    LOG_INFO_LEVEL  = 2,
    LOG_DEBUG_LEVEL = 3
};

#define LOG_PRINT(level, fmt, ...) do { if (LOG_LEVEL >= level) Serial.printf(fmt, ##__VA_ARGS__); } while(0)
#define LOGE(fmt, ...) LOG_PRINT(LOG_ERROR_LEVEL, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT(LOG_WARN_LEVEL, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_PRINT(LOG_INFO_LEVEL, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_PRINT(LOG_DEBUG_LEVEL, fmt, ##__VA_ARGS__)

// ===== Globals exposed to API =====
float g_lastProb  = 0.0f;
float g_lastScore = 0.0f;
bool  g_isCrying  = false;
double g_lastLat  = 0.0;
double g_lastLng  = 0.0;
bool   g_gpsValid = false;
char   g_statusMessage[64] = "Đang khởi động hệ thống...";

// ===== FreeRTOS handles =====
static TaskHandle_t hMicTask    = nullptr;
static TaskHandle_t hInferTask  = nullptr;
static TaskHandle_t hGpsTask    = nullptr;
static TaskHandle_t hSendTask   = nullptr;
static TaskHandle_t hWifiTask   = nullptr;
static QueueHandle_t qPcm       = nullptr;
static QueueHandle_t qPcmFree   = nullptr;
static QueueHandle_t qEvents    = nullptr;
static constexpr uint32_t REMIND_INTERVAL_MS = 180000; // 3 phút
#if USE_MAX98357A_SPK
static QueueHandle_t qSpeaker   = nullptr;
enum class SpeakCmd : uint8_t { Cry, Calm, NightOn, NightOff, Ting };
#endif
bool nightMode = false; // bật chế độ đêm nếu cần
static uint32_t g_lastCryChangeMs = 0;
static uint32_t g_lastCryReminderMs = 0;
static EventGroupHandle_t g_wifiEventGroup = nullptr;
static constexpr EventBits_t WIFI_READY_BIT = BIT0;
static constexpr uint32_t WIFI_BACKOFF_MIN_MS = 1000;
static constexpr uint32_t WIFI_BACKOFF_MAX_MS = 10000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t PCM_POOL_BUFFERS = 8;
static constexpr size_t TARGET_SAMPLES = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S);
static constexpr uint32_t CRY_HEARTBEAT_INTERVAL_MS = 15000;
static bool g_setupApActive = false;
static String g_setupApSsid;
static bool g_wifiReconnectRequest = false;
static constexpr const char* CONFIG_AP_PASS = "crysetup";
static WiFiEventId_t g_wifiEventId = 0;
static uint8_t g_wifiFailCount = 0;
static uint8_t g_lastWifiReason = WIFI_REASON_UNSPECIFIED;
static bool g_wifiConnected = false;
static bool g_wifiPausedAfterFail = false;
const char* g_lastEvent = "idle";
uint32_t g_lastEventTs = 0;
static bool g_btnPrev = true;
static uint32_t g_btnLastMs = 0;
static uint32_t g_lastHeartbeatReportMs = 0;
static int16_t* g_inferWindow = nullptr;
static uint32_t g_lastInferAllocLogMs = 0;

struct CryEvent {
    bool crying;
    bool heartbeat;
    float prob;
    float score;
    double lat;
    double lng;
    bool gpsValid;
    uint32_t tsMs;
    uint32_t durationMs;
};

// ===== Cry debouncer =====
static CryDetector detector(
    0.70f, 0.18f, 0.15f,
    1.0f, 2.3f,
    2.0f, 1.5f,
    0.25f,
    INFER_INTERVAL_S
);

struct DetectorProfile {
    float onTh;
    float offTh;
    float stableOn;
    float stableOff;
    float minOn;
    float minOff;
};

static constexpr DetectorProfile DAY_PROFILE{
    0.70f, 0.18f,
    1.0f, 2.3f,
    2.0f, 1.5f
};

static constexpr DetectorProfile NIGHT_PROFILE{
    0.78f, 0.22f,
    1.5f, 2.8f,
    2.5f, 2.0f
};

static void setStatusMessage(const char* msg){
    if (!msg) return;
    size_t len = strlen(msg);
    if (len >= sizeof(g_statusMessage)) len = sizeof(g_statusMessage)-1;
    memcpy(g_statusMessage, msg, len);
    g_statusMessage[len] = '\0';
}

static void applyDetectorProfile(){
    const DetectorProfile& profile = nightMode ? NIGHT_PROFILE : DAY_PROFILE;
    detector.configure(profile.onTh, profile.offTh,
                       profile.stableOn, profile.stableOff,
                       profile.minOn, profile.minOff);
}

static void* audio_malloc(size_t bytes){
    void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr){
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool ensure_infer_window(){
    if (g_inferWindow){
        return true;
    }
    const size_t bytes = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S) * sizeof(int16_t);
    g_inferWindow = static_cast<int16_t*>(audio_malloc(bytes));
    if (!g_inferWindow){
        uint32_t now = millis();
        if (now - g_lastInferAllocLogMs > 1000){
            LOGE("[AI] Failed to allocate inference window buffer, retrying...");
            g_lastInferAllocLogMs = now;
        }
        return false;
    }
    return true;
}

static bool initPcmPool(){
    if (qPcmFree){
        return true;
    }
    qPcmFree = xQueueCreate(PCM_POOL_BUFFERS, sizeof(int16_t*));
    if (!qPcmFree){
        return false;
    }
    uint32_t allocated = 0;
    for (uint32_t i=0; i<PCM_POOL_BUFFERS; ++i){
        int16_t* buf = static_cast<int16_t*>(audio_malloc(I2S_READ_LEN * sizeof(int16_t)));
        if (!buf){
            break;
        }
        xQueueSend(qPcmFree, &buf, 0);
        allocated++;
    }
    return allocated > 0;
}

static inline int16_t* acquirePcmBlock(TickType_t timeoutTicks){
    int16_t* block = nullptr;
    if (qPcmFree && xQueueReceive(qPcmFree, &block, timeoutTicks) == pdTRUE){
        return block;
    }
    return nullptr;
}

static inline void releasePcmBlock(int16_t* block){
    if (!block) return;
    if (!qPcmFree || xQueueSend(qPcmFree, &block, 0) != pdTRUE){
        heap_caps_free(block);
    }
}

static void ensure_setup_ap(){
    if (g_setupApActive) return;
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
    g_setupApSsid = String("AudioCry-Setup-") + suffix;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(g_setupApSsid.c_str(), CONFIG_AP_PASS);
    Serial.printf("[WiFi] AP cấu hình bật: %s / %s\n", g_setupApSsid.c_str(), CONFIG_AP_PASS);
    g_setupApActive = true;
}

static void stop_setup_ap(){
    if (!g_setupApActive) return;
    WiFi.softAPdisconnect(true);
    g_setupApActive = false;
}

static String get_setup_portal_url(){
    if (!g_setupApActive) return String();
    IPAddress ip = WiFi.softAPIP();
    return String("http://") + ip.toString() + "/wifi";
}

static void updateWifiLed(){
    digitalWrite(LED_WIFI_PIN, g_wifiConnected ? HIGH : LOW);
}

static void updateCryLed(bool crying){
    digitalWrite(LED_CRY_RED_PIN, crying ? HIGH : LOW);
    digitalWrite(LED_CRY_GREEN_PIN, crying ? LOW : HIGH);
}

static void blinkWifiLed(uint8_t times){
    for (uint8_t i=0;i<times;i++){
        digitalWrite(LED_WIFI_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(80));
        digitalWrite(LED_WIFI_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    updateWifiLed();
}

static void wifi_mark_connected(){
    g_wifiConnected = true;
    updateWifiLed();
    if (g_wifiEventGroup){
        xEventGroupSetBits(g_wifiEventGroup, WIFI_READY_BIT);
    }
}

static void wifi_mark_disconnected(){
    g_wifiConnected = false;
    updateWifiLed();
    if (g_wifiEventGroup){
        xEventGroupClearBits(g_wifiEventGroup, WIFI_READY_BIT);
    }
}

#if USE_MAX98357A_SPK
static void speaker_write(const int16_t* data, size_t samples){
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
    size_t bytesRemaining = samples * sizeof(int16_t);
    while (bytesRemaining > 0){
        size_t written = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, ptr, bytesRemaining, &written, pdMS_TO_TICKS(200));
        if (err != ESP_OK || written == 0){
            break;
        }
        ptr += written;
        bytesRemaining -= written;
    }
}

static void speaker_play_phrase(const int16_t* data, size_t samples){
    constexpr size_t CHUNK_SAMPLES = 256;
    static int16_t chunk[CHUNK_SAMPLES];
    size_t offset = 0;
    while (offset < samples){
        size_t copy = std::min(CHUNK_SAMPLES, samples - offset);
        memcpy(chunk, data + offset, copy * sizeof(int16_t));
        speaker_write(chunk, copy);
        offset += copy;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void speaker_play_cmd(SpeakCmd cmd){
    switch(cmd){
        case SpeakCmd::Cry:
            speaker_play_phrase(gSpeechCry, gSpeechCry_LEN);
            break;
        case SpeakCmd::Calm:
            speaker_play_phrase(gSpeechCalm, gSpeechCalm_LEN);
            break;
        case SpeakCmd::NightOn:
            speaker_play_phrase(gSpeechNightOn, gSpeechNightOn_LEN);
            break;
        case SpeakCmd::NightOff:
            speaker_play_phrase(gSpeechNightOff, gSpeechNightOff_LEN);
            break;
        case SpeakCmd::Ting: {
            constexpr size_t N = 3200; // ~200ms @16kHz
            static int16_t buf[N];
            static bool inited = false;
            if (!inited){
                for (size_t i=0;i<N;++i){
                    float env = sinf(3.14159f * i / N);
                    float s = sinf(2.0f*3.14159f*600.0f*i/16000.0f)*env;
                    buf[i] = static_cast<int16_t>(s*12000.0f);
                }
                inited = true;
            }
            speaker_write(buf, N);
            break;
        }
    }
}

static void taskSpeaker(void*){
    SpeakCmd cmd = SpeakCmd::Calm;
    for(;;){
        if (xQueueReceive(qSpeaker, &cmd, portMAX_DELAY) == pdTRUE){
            speaker_play_cmd(cmd);
        }
    }
}

static void speaker_clear_queue(){
    if (!qSpeaker) return;
    SpeakCmd tmp;
    while (xQueueReceive(qSpeaker, &tmp, 0) == pdTRUE){
        // discard pending audio commands to avoid spam
    }
}

static void speaker_enqueue(SpeakCmd cmd, bool priority=false){
    if (!qSpeaker) return;
    static SpeakCmd lastCmd = SpeakCmd::Calm;
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (priority){
        speaker_clear_queue();
    } else if (cmd == lastCmd && (now - lastMs) < 2000){
        return; // bỏ qua lệnh trùng quá gần tránh spam
    }
    if (xQueueSend(qSpeaker, &cmd, 0) == pdTRUE){
        lastCmd = cmd;
        lastMs = now;
    }
}
#else
static void speaker_enqueue(...) {}
#endif

static const char* wifi_reason_to_text(uint8_t reason){
    switch(reason){
        case WIFI_REASON_NO_AP_FOUND: return "không tìm thấy SSID";
        case WIFI_REASON_AUTH_FAIL: return "sai mật khẩu";
        case WIFI_REASON_BEACON_TIMEOUT: return "mất tín hiệu AP";
        case WIFI_REASON_ASSOC_LEAVE: return "AP ngắt kết nối";
        default: return "lý do khác";
    }
}

static void handle_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info){
    switch(event){
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOGI("[WiFi] Đã kết nối tới AP %s\n", WiFi.SSID().c_str());
            wifi_mark_connected();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            g_lastWifiReason = info.wifi_sta_disconnected.reason;
            if (info.wifi_sta_disconnected.reason != WIFI_REASON_NO_AP_FOUND){
                LOGW("[WiFi] Mất kết nối (reason=%d - %s). Sẽ thử lại...\n",
                     info.wifi_sta_disconnected.reason,
                     wifi_reason_to_text(info.wifi_sta_disconnected.reason));
            }
            wifi_mark_disconnected();
            break;
        default:
            break;
    }
}

// ===== I2S setup =====
static void i2s_init() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
#if USE_MAX98357A_SPK
    cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX);
#endif
    cfg.sample_rate = I2S_SAMPLE_RATE;
    cfg.bits_per_sample = static_cast<i2s_bits_per_sample_t>(I2S_BITS_PER_SAMP);
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = I2S_READ_LEN;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_SCK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
#if USE_MAX98357A_SPK
    pins.data_out_num = I2S_SD_OUT_PIN;
#else
    pins.data_out_num = -1;
#endif
    pins.data_in_num  = I2S_SD_PIN;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_set_clk(I2S_NUM_0, I2S_SAMPLE_RATE, (i2s_bits_per_sample_t)I2S_BITS_PER_SAMP, I2S_CHANNEL_MONO);
}

// ===== MIC Task: read PCM blocks =====
static void taskMic(void* arg) {
    esp_task_wdt_add(nullptr);
    const size_t CHUNK = I2S_READ_LEN;
    size_t bytes_read = 0;
    uint32_t lastLog=0;
    uint32_t blocksOk=0;
    for(;;){
        int16_t* block = acquirePcmBlock(pdMS_TO_TICKS(50));
        if (!block){
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        esp_err_t err = i2s_read(I2S_NUM_0, block, CHUNK * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (err != ESP_OK || bytes_read != CHUNK * sizeof(int16_t)) {
            releasePcmBlock(block);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (xQueueSend(qPcm, &block, 0) != pdTRUE) {
            releasePcmBlock(block);
        }
        blocksOk++;
        uint32_t now = millis();
        if (now - lastLog > 10000){
            int16_t mid = block[CHUNK/2];
            Serial.printf("[MIC] %u blocks OK, mid=%d\n", blocksOk, mid);
            lastLog = now;
            blocksOk=0;
        }
        esp_task_wdt_reset();
    }
}
// ===== Infer Task =====
static void taskInfer(void* arg) {
    const TickType_t delayTicks = (TickType_t)(INFER_INTERVAL_S*1000)/portTICK_PERIOD_MS;
    while (!ensure_infer_window()){
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    int16_t* win = g_inferWindow;
    size_t filled = 0;
    bool prevState=false;
    while (!tflm_begin()){
        LOGE("[AI] Failed to init TFLM, retrying...\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_task_wdt_add(nullptr);
    g_lastHeartbeatReportMs = millis();

    for(;;){
        while (filled < TARGET_SAMPLES){
            int16_t* block=nullptr;
            if (xQueueReceive(qPcm, &block, pdMS_TO_TICKS(10)) == pdTRUE && block){
                size_t copy = min(TARGET_SAMPLES - filled, (size_t)I2S_READ_LEN);
                memcpy(win + filled, block, copy*sizeof(int16_t));
                filled += copy;
                releasePcmBlock(block);
            } else {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            esp_task_wdt_reset();
        }
        float prob = tflm_infer_prob(win, TARGET_SAMPLES);
        g_lastProb = prob;
        bool state = detector.update(prob);
        g_lastScore = detector.score();
        g_isCrying = state;
        LOGI("[AI] prob=%.3f score=%.3f state=%s (night=%s)\n",
             prob, g_lastScore, state ? "CRY" : "CALM", nightMode ? "ON" : "OFF");

        uint32_t now = millis();
        if (state != prevState){
            GpsFix fix = gps_get_fix();
            g_lastLat = fix.lat; g_lastLng = fix.lng; g_gpsValid = fix.valid;
            updateCryLed(state);
            if (nightMode){
                speaker_enqueue(SpeakCmd::Ting);
            } else {
                speaker_enqueue(state ? SpeakCmd::Cry : SpeakCmd::Calm);
            }
            CryEvent evt{};
            evt.crying = state;
            evt.heartbeat = false;
            evt.prob = prob;
            evt.score = g_lastScore;
            evt.lat = g_lastLat;
            evt.lng = g_lastLng;
            evt.gpsValid = g_gpsValid;
            evt.tsMs = now;
            evt.durationMs = state ? 0 : (now - g_lastCryChangeMs);
            g_lastCryChangeMs = evt.tsMs;
            g_lastCryReminderMs = evt.tsMs;
            g_lastHeartbeatReportMs = evt.tsMs;
            g_lastEvent = state ? "cry_on" : "cry_off";
            g_lastEventTs = evt.tsMs;
            Serial.printf("[AI] State change -> %s (prob=%.3f score=%.3f gps=%s lat=%.5f lng=%.5f)\n",
                          state ? "CRY_ON" : "CRY_OFF",
                          evt.prob, evt.score,
                          evt.gpsValid ? "valid" : "invalid",
                          evt.lat, evt.lng);
            if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(50)) != pdTRUE) {
                Serial.println("Event queue full, dropping cry event");
            }
            prevState = state;
        } else {
            if (state && (now - g_lastCryReminderMs) > REMIND_INTERVAL_MS) {
                g_lastCryReminderMs = now;
                Serial.println("[AI] Cry reminder trigger, still detecting cry state");
                if (nightMode){
                    speaker_enqueue(SpeakCmd::Ting);
                } else {
                    speaker_enqueue(SpeakCmd::Cry);
                }
            }
            if (state && (now - g_lastHeartbeatReportMs) > CRY_HEARTBEAT_INTERVAL_MS){
                CryEvent evt{};
                evt.crying = true;
                evt.heartbeat = true;
                evt.prob = prob;
                evt.score = g_lastScore;
                evt.lat = g_lastLat;
                evt.lng = g_lastLng;
                evt.gpsValid = g_gpsValid;
                evt.tsMs = now;
                evt.durationMs = now - g_lastCryChangeMs;
                if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
                    Serial.println("Heartbeat queue full, dropping cry heartbeat");
                } else {
                    g_lastHeartbeatReportMs = now;
                }
            }
        }
        filled = 0;
        esp_task_wdt_reset();
        vTaskDelay(delayTicks);
    }
}

// ===== GPS Task =====// ===== GPS Task =====
static void taskGps(void* arg){
    uint32_t lastLog=0;
    for(;;){
        gps_loop();
        if (millis()-lastLog>10000){
            GpsFix fix = gps_get_fix();
            if (fix.valid){
                Serial.printf("[GPS] lat=%.5f lng=%.5f tuổi=%lu ms\n", fix.lat, fix.lng, fix.age_ms);
            } else {
                Serial.println("[GPS] Đang tìm vệ tinh...");
            }
            lastLog = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static bool wifi_connect_blocking(){
    if (g_wifiPausedAfterFail){
        LOGW("[WiFi] Đang tạm dừng thử lại cho tới khi cấu hình mới.");
        setStatusMessage("Đang dùng AP cấu hình, vui lòng nhập WiFi mới.");
        ensure_setup_ap();
        wifi_mark_disconnected();
        return false;
    }
    WifiCredentials creds;
    wifi_config_load(creds);
    if (creds.ssid.isEmpty()){
        LOGW("[WiFi] Chưa có thông tin WiFi, bật AP cấu hình.");
        setStatusMessage("Chưa cấu hình WiFi, kết nối AP để nhập.");
        ensure_setup_ap();
        String portal = get_setup_portal_url();
        if (portal.length()){
            LOGI("[WiFi] Mở %s để nhập WiFi.\n", portal.c_str());
            String msg = String("Chưa có WiFi, mở ") + portal;
            setStatusMessage(msg.c_str());
        }
        g_wifiPausedAfterFail = true;
        wifi_mark_disconnected();
        return false;
    }
    if (g_wifiEventId == 0){
        g_wifiEventId = WiFi.onEvent(handle_wifi_event);
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(true);
    LOGI("[WiFi] Đang kết nối tới \"%s\"...\n", creds.ssid.c_str());
    setStatusMessage("Đang chờ kết nối WiFi...");
    WiFi.begin(creds.ssid.c_str(), creds.pass.c_str());
    uint32_t t0=millis();
    while (WiFi.status()!=WL_CONNECTED && millis()-t0<WIFI_CONNECT_TIMEOUT_MS){
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    bool ok = WiFi.status()==WL_CONNECTED;
    if (ok){
        LOGI("[WiFi] Kết nối thành công, IP: %s\n", WiFi.localIP().toString().c_str());
        String cfgUrl = String("http://") + WiFi.localIP().toString() + "/wifi";
        LOGI("[WiFi] Trang cấu hình: %s\n", cfgUrl.c_str());
        setStatusMessage("WiFi đã kết nối, đang khởi tạo...");
        g_wifiFailCount = 0;
        g_wifiPausedAfterFail = false;
        wifi_mark_connected();
        stop_setup_ap();
    } else {
        g_wifiFailCount = std::min<uint8_t>(g_wifiFailCount + 1, 10);
        LOGW("[WiFi] Kết nối thất bại (reason=%s).\n", wifi_reason_to_text(g_lastWifiReason));
        wifi_mark_disconnected();
        ensure_setup_ap();
        if (!g_wifiPausedAfterFail){
            g_wifiPausedAfterFail = true;
            LOGW("[WiFi] Tạm ngưng thử lại cho tới khi nhập WiFi mới hoặc reboot.");
        }
        setStatusMessage("Không kết nối được, vui lòng dùng AP cấu hình.");
        String portal = get_setup_portal_url();
        if (portal.length()){
            LOGI("[WiFi] Vui lòng mở %s để nhập lại WiFi.\n", portal.c_str());
            String msg = String("Không kết nối, mở ") + portal;
            setStatusMessage(msg.c_str());
        }
    }
    return ok;
}
static bool ensure_wifi(uint32_t waitMs = 5000){
    if (!g_wifiEventGroup){
        return WiFi.status()==WL_CONNECTED;
    }
    EventBits_t bits = xEventGroupWaitBits(
        g_wifiEventGroup,
        WIFI_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(waitMs)
    );
    return (bits & WIFI_READY_BIT);
}

void wifi_request_reconnect(){
    g_wifiPausedAfterFail = false;
    g_wifiReconnectRequest = true;
    wifi_mark_disconnected();
}

bool wifi_is_setup_ap_active(){
    return g_setupApActive;
}

const char* wifi_get_setup_ap_ssid(){
    return g_setupApSsid.c_str();
}

const char* wifi_get_setup_ap_pass(){
    return CONFIG_AP_PASS;
}

static void taskWifi(void*){
    uint32_t backoffMs = WIFI_BACKOFF_MIN_MS;
    for(;;){
        if (g_wifiReconnectRequest){
            g_wifiReconnectRequest = false;
            g_wifiPausedAfterFail = false;
            WiFi.disconnect(true, true);
            wifi_mark_disconnected();
            backoffMs = WIFI_BACKOFF_MIN_MS;
        }
        if (g_wifiPausedAfterFail){
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (WiFi.status()!=WL_CONNECTED){
            bool ok = wifi_connect_blocking();
            if (ok){
                backoffMs = WIFI_BACKOFF_MIN_MS;
            } else {
                backoffMs = std::min(backoffMs * 2, WIFI_BACKOFF_MAX_MS);
                vTaskDelay(pdMS_TO_TICKS(backoffMs));
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void taskSender(void* arg){
    CryEvent evt;
    for(;;){
        if (xQueueReceive(qEvents, &evt, portMAX_DELAY) != pdTRUE){
            continue;
        }
        while (!ensure_wifi(15000)){
            LOGW("[Send] WiFi chưa sẵn sàng, chờ gửi sự kiện...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        String json = "{";
        json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
        const char* evtName = evt.heartbeat ? "cry_heartbeat" : (evt.crying ? "cry_on" : "cry_off");
        json += "\"event\":\"" + String(evtName) + "\",";
        json += "\"prob\":" + String(evt.prob, 3) + ",";
        json += "\"score\":" + String(evt.score, 3) + ",";
        json += "\"lat\":" + String(evt.lat, 6) + ",";
        json += "\"lng\":" + String(evt.lng, 6) + ",";
        json += "\"gps_valid\":"; json += (evt.gpsValid ? "true" : "false"); json += ",";
        json += "\"duration_ms\":" + String(evt.durationMs) + ",";
        json += "\"ts\":" + String(evt.tsMs);
        json += "}";
        bool sent = false;
        for (uint8_t attempt=0; attempt<3 && !sent; ++attempt){
            if (RestClient::postJSON(BACKEND_URL, json)){
                sent = true;
            } else {
                LOGW("[Send] Gửi sự kiện thất bại lần %u", attempt+1);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        if (!sent){
            LOGE("[Send] Bỏ sự kiện khóc sau khi thử nhiều lần");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void initRuntime(){
    Serial.begin(115200); delay(200);
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("WiFiGeneric", ESP_LOG_NONE);
    esp_log_level_set("WiFi", ESP_LOG_NONE);
    esp_task_wdt_init(5, true);
    wifi_config_init();
    ensure_setup_ap();
    applyDetectorProfile();
    if (!g_wifiEventGroup){
        g_wifiEventGroup = xEventGroupCreate();
        if (!g_wifiEventGroup){
            Serial.println("[WiFi] Failed to create event group");
        }
    }
    setStatusMessage("Đang khởi động hệ thống...");
}
static void initGpioAndStatus(){
    pinMode(LED_WIFI_PIN, OUTPUT);
    pinMode(LED_CRY_RED_PIN, OUTPUT);
    pinMode(LED_CRY_GREEN_PIN, OUTPUT);
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    updateWifiLed();
    updateCryLed(false);
}

static void initQueues(){
    qPcm = xQueueCreate(PCM_POOL_BUFFERS, sizeof(int16_t*));
    qEvents = xQueueCreate(6, sizeof(CryEvent));
#if USE_MAX98357A_SPK
    qSpeaker = xQueueCreate(4, sizeof(SpeakCmd));
#endif
    if (!qPcm || !qEvents){
        Serial.println("[Init] Failed to allocate queues");
    }
    if (!initPcmPool()){
        Serial.println("[Init] Failed to init PCM buffer pool");
    }
    if (!ensure_infer_window()){
        Serial.println("[Init] Inference buffer not ready yet, will retry in task");
    }
}

static void startTasks(){
    xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 2, &hWifiTask, 0);
    xTaskCreatePinnedToCore(taskMic, "mic", 4096, nullptr, 2, &hMicTask, 0);
    xTaskCreatePinnedToCore(taskInfer, "infer", 8192, nullptr, 3, &hInferTask, 1);
    xTaskCreatePinnedToCore(taskGps, "gps", 3072, nullptr, 1, &hGpsTask, 1);
    xTaskCreatePinnedToCore(taskSender, "sender", 4096, nullptr, 1, &hSendTask, 1);
#if USE_MAX98357A_SPK
    if (qSpeaker){
        xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, nullptr, 1);
    }
#endif
}

static void handleModeButton(uint32_t now){
    constexpr uint32_t DEBOUNCE_MS = 60;
    constexpr uint32_t MIN_PRESS_MS = 120;
    constexpr uint32_t MAX_PRESS_MS = 4000;
    constexpr uint32_t TOGGLE_GAP_MS = 900;
    constexpr uint32_t IGNORE_AFTER_RESET_MS = 1200;
    static bool rawPrev = true;
    static bool stableState = true;
    static uint32_t lastModeToggleMs = 0;
    static uint32_t pressStartMs = 0;
    static uint32_t lastChangeMs = 0;

    bool rawNow = (digitalRead(MODE_BUTTON_PIN) == LOW);
    if (rawNow != rawPrev){
        rawPrev = rawNow;
        lastChangeMs = now;
    }
    if ((now - lastChangeMs) > DEBOUNCE_MS && stableState != rawPrev){
        stableState = rawPrev;
        if (!stableState){
            pressStartMs = now;
        } else {
            if (pressStartMs > 0){
                uint32_t pressDur = now - pressStartMs;
                bool canToggle = (now > IGNORE_AFTER_RESET_MS) &&
                                 (pressDur >= MIN_PRESS_MS && pressDur <= MAX_PRESS_MS) &&
                                 ((now - lastModeToggleMs) > TOGGLE_GAP_MS);
                if (canToggle){
                    nightMode = !nightMode;
                    lastModeToggleMs = now;
                    Serial.printf("[MODE] nightMode=%s\n", nightMode ? "ON" : "OFF");
                    applyDetectorProfile();
                    blinkWifiLed(nightMode ? 2 : 1);
                    speaker_enqueue(nightMode ? SpeakCmd::NightOn : SpeakCmd::NightOff, true);
                }
            }
            pressStartMs = 0;
        }
    }
}

void setup(){
    initRuntime();
    initGpioAndStatus();
    api_begin();
    i2s_init();
    gps_begin();
    initQueues();
    startTasks();
    g_wifiReconnectRequest = true;
    setStatusMessage("Hệ thống đang nghe âm thanh...");
}
void loop(){
    uint32_t now = millis();
    handleModeButton(now);

    api_loop();
    delay(5);
}







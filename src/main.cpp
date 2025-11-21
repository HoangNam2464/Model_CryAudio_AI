#include <Arduino.h>
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
#include "api_server.h"
#include <CryDetector.h>
#include <RestClient.h>
#include <gps.h>
#include <tflm_infer.h>
#include <driver/gpio.h>
#include "wifi_service.h"
#if USE_MAX98357A_SPK
#include "SpeechSamples.h"
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

enum LogVerbosity
{
    LOG_ERROR_LEVEL = 0,
    LOG_WARN_LEVEL = 1,
    LOG_INFO_LEVEL = 2,
    LOG_DEBUG_LEVEL = 3
};

#define LOG_PRINT(level, fmt, ...)             \
    do                                         \
    {                                          \
        if (LOG_LEVEL >= level)                \
            Serial.printf(fmt, ##__VA_ARGS__); \
    } while (0)
#define LOGE(fmt, ...) LOG_PRINT(LOG_ERROR_LEVEL, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT(LOG_WARN_LEVEL, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_PRINT(LOG_INFO_LEVEL, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_PRINT(LOG_DEBUG_LEVEL, fmt, ##__VA_ARGS__)

// ===== Globals exposed to API =====
float g_lastProb = 0.0f;
float g_lastScore = 0.0f;
bool g_isCrying = false;
double g_lastLat = 0.0;
double g_lastLng = 0.0;
bool g_gpsValid = false;
char g_statusMessage[64] = "Đang khởi động hệ thống...";

// ===== FreeRTOS handles =====
static TaskHandle_t hMicTask = nullptr;
static TaskHandle_t hInferTask = nullptr;
static TaskHandle_t hGpsTask = nullptr;
static TaskHandle_t hSendTask = nullptr;
static QueueHandle_t qPcm = nullptr;
static QueueHandle_t qPcmFree = nullptr;
static QueueHandle_t qEvents = nullptr;
static constexpr uint32_t REMIND_INTERVAL_MS = 180000; // 3 phút
#if USE_MAX98357A_SPK
static QueueHandle_t qSpeaker = nullptr;
enum class SpeakCmd : uint8_t
{
    Cry,
    Calm,
    NightOn,
    NightOff,
    Ting
};
#endif
bool nightMode = false; // bật chế độ đêm nếu cần
static uint32_t g_lastCryChangeMs = 0;
static uint32_t g_lastCryReminderMs = 0;
static constexpr uint32_t PCM_POOL_BUFFERS = 4; // giảm số buffer để tiết kiệm RAM
static constexpr size_t TARGET_SAMPLES = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S);
static constexpr uint32_t CRY_HEARTBEAT_INTERVAL_MS = 15000;
const char *g_lastEvent = "idle";
uint32_t g_lastEventTs = 0;
static bool g_btnPrev = true;
static uint32_t g_btnLastMs = 0;
static uint32_t g_lastHeartbeatReportMs = 0;
static int16_t *g_inferWindow = nullptr;
static uint32_t g_lastInferAllocLogMs = 0;
static uint32_t g_pcmBuffersAllocated = 0;

struct CryEvent
{
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
    INFER_INTERVAL_S);

struct DetectorProfile
{
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
    2.0f, 1.5f};

static constexpr DetectorProfile NIGHT_PROFILE{
    0.78f, 0.22f,
    1.5f, 2.8f,
    2.5f, 2.0f};

void setStatusMessage(const char *msg)
{
    if (!msg)
        return;
    size_t len = strlen(msg);
    if (len >= sizeof(g_statusMessage))
        len = sizeof(g_statusMessage) - 1;
    memcpy(g_statusMessage, msg, len);
    g_statusMessage[len] = '\0';
}

static void applyDetectorProfile()
{
    const DetectorProfile &profile = nightMode ? NIGHT_PROFILE : DAY_PROFILE;
    detector.configure(profile.onTh, profile.offTh,
                       profile.stableOn, profile.stableOff,
                       profile.minOn, profile.minOff);
}

static void *audio_malloc(size_t bytes)
{
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr)
    {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool ensure_infer_window()
{
    if (g_inferWindow)
    {
        return true;
    }
    const size_t bytes = static_cast<size_t>(I2S_SAMPLE_RATE * INFER_INTERVAL_S) * sizeof(int16_t);
    g_inferWindow = static_cast<int16_t *>(audio_malloc(bytes));
    if (!g_inferWindow)
    {
        uint32_t now = millis();
        if (now - g_lastInferAllocLogMs > 1000)
        {
            LOGE("[AI] Failed to allocate inference window buffer, retrying...");
            g_lastInferAllocLogMs = now;
        }
        return false;
    }
    LOGI("[AI] Inference buffer ready (%u samples)\n", (unsigned)(bytes / sizeof(int16_t)));
    return true;
}

static bool initPcmPool()
{
    if (qPcmFree)
    {
        return true;
    }
    qPcmFree = xQueueCreate(PCM_POOL_BUFFERS, sizeof(int16_t *));
    if (!qPcmFree)
    {
        return false;
    }
    uint32_t allocated = 0;
    for (uint32_t i = 0; i < PCM_POOL_BUFFERS; ++i)
    {
        int16_t *buf = static_cast<int16_t *>(audio_malloc(I2S_READ_LEN * sizeof(int16_t)));
        if (!buf)
        {
            break;
        }
        xQueueSend(qPcmFree, &buf, 0);
        allocated++;
    }
    g_pcmBuffersAllocated = allocated;
    if (allocated > 0)
    {
        LOGI("[Init] Allocated %u PCM buffers\n", allocated);
    }
    else
    {
        LOGE("[Init] Failed to allocate PCM buffers");
    }
    return allocated > 0;
}

static inline int16_t *acquirePcmBlock(TickType_t timeoutTicks)
{
    int16_t *block = nullptr;
    if (qPcmFree && xQueueReceive(qPcmFree, &block, timeoutTicks) == pdTRUE)
    {
        return block;
    }
    return nullptr;
}

static inline void releasePcmBlock(int16_t *block)
{
    if (!block)
        return;
    if (!qPcmFree || xQueueSend(qPcmFree, &block, 0) != pdTRUE)
    {
        heap_caps_free(block);
    }
}

static void updateCryLed(bool crying)
{
    digitalWrite(LED_CRY_RED_PIN, crying ? HIGH : LOW);
    digitalWrite(LED_CRY_GREEN_PIN, crying ? LOW : HIGH);
}

#if USE_MAX98357A_SPK
static void speaker_write(const int16_t *data, size_t samples)
{
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);
    size_t bytesRemaining = samples * sizeof(int16_t);
    while (bytesRemaining > 0)
    {
        size_t written = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, ptr, bytesRemaining, &written, pdMS_TO_TICKS(200));
        if (err != ESP_OK || written == 0)
        {
            break;
        }
        ptr += written;
        bytesRemaining -= written;
    }
}

static void speaker_play_phrase(const int16_t *data, size_t samples)
{
    constexpr size_t CHUNK_SAMPLES = 256;
    static int16_t chunk[CHUNK_SAMPLES];
    size_t offset = 0;
    while (offset < samples)
    {
        size_t copy = std::min(CHUNK_SAMPLES, samples - offset);
        memcpy(chunk, data + offset, copy * sizeof(int16_t));
        speaker_write(chunk, copy);
        offset += copy;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void speaker_play_cmd(SpeakCmd cmd)
{
    switch (cmd)
    {
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
    case SpeakCmd::Ting:
    {
        constexpr size_t N = 3200; // ~200ms @16kHz
        static int16_t buf[N];
        static bool inited = false;
        if (!inited)
        {
            for (size_t i = 0; i < N; ++i)
            {
                float env = sinf(3.14159f * i / N);
                float s = sinf(2.0f * 3.14159f * 600.0f * i / 16000.0f) * env;
                buf[i] = static_cast<int16_t>(s * 12000.0f);
            }
            inited = true;
        }
        speaker_write(buf, N);
        break;
    }
    }
}

static void taskSpeaker(void *)
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

static void speaker_clear_queue()
{
    if (!qSpeaker)
        return;
    SpeakCmd tmp;
    while (xQueueReceive(qSpeaker, &tmp, 0) == pdTRUE)
    {
        // discard pending audio commands to avoid spam
    }
}

static void speaker_enqueue(SpeakCmd cmd, bool priority = false)
{
    if (!qSpeaker)
        return;
    static SpeakCmd lastCmd = SpeakCmd::Calm;
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (priority)
    {
        speaker_clear_queue();
    }
    else if (cmd == lastCmd && (now - lastMs) < 2000)
    {
        return; // bỏ qua lệnh trùng quá gần tránh spam
    }
    if (xQueueSend(qSpeaker, &cmd, 0) == pdTRUE)
    {
        lastCmd = cmd;
        lastMs = now;
    }
}
#else
static void speaker_enqueue(...) {}
#endif

// ===== I2S setup =====
static void i2s_init()
{
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
    pins.bck_io_num = I2S_SCK_PIN;
    pins.ws_io_num = I2S_WS_PIN;
#if USE_MAX98357A_SPK
    pins.data_out_num = I2S_SD_OUT_PIN;
#else
    pins.data_out_num = -1;
#endif
    pins.data_in_num = I2S_SD_PIN;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_set_clk(I2S_NUM_0, I2S_SAMPLE_RATE, (i2s_bits_per_sample_t)I2S_BITS_PER_SAMP, I2S_CHANNEL_MONO);
}

// ===== MIC Task: read PCM blocks =====
static void taskMic(void *arg)
{
    esp_task_wdt_add(nullptr);
    const size_t CHUNK = I2S_READ_LEN;
    size_t bytes_read = 0;
    uint32_t lastLog = 0;
    uint32_t blocksOk = 0;
    for (;;)
    {
        int16_t *block = acquirePcmBlock(pdMS_TO_TICKS(50));
        if (!block)
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        esp_err_t err = i2s_read(I2S_NUM_0, block, CHUNK * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (err != ESP_OK || bytes_read != CHUNK * sizeof(int16_t))
        {
            releasePcmBlock(block);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (xQueueSend(qPcm, &block, 0) != pdTRUE)
        {
            releasePcmBlock(block);
            LOGW("[MIC] qPcm full, dropping block");
        }
        blocksOk++;
        uint32_t now = millis();
        if (now - lastLog > 10000)
        {
            int16_t mid = block[CHUNK / 2];
            Serial.printf("[MIC] %u blocks OK, mid=%d\n", blocksOk, mid);
            lastLog = now;
            blocksOk = 0;
        }
        else if ((blocksOk % 50) == 0)
        {
            LOGD("[MIC] queued %u blocks", blocksOk);
        }
        esp_task_wdt_reset();
    }
}
// ===== Infer Task =====
static void taskInfer(void *arg)
{
    const TickType_t delayTicks = (TickType_t)(INFER_INTERVAL_S * 1000) / portTICK_PERIOD_MS;
    while (!ensure_infer_window())
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    int16_t *win = g_inferWindow;
    size_t filled = 0;
    bool prevState = false;
    uint32_t lastIdleLog = 0;
    while (!tflm_begin())
    {
        LOGE("[AI] Failed to init TFLM, retrying...\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_task_wdt_add(nullptr);
    g_lastHeartbeatReportMs = millis();

    for (;;)
    {
        while (filled < TARGET_SAMPLES)
        {
            int16_t *block = nullptr;
            if (xQueueReceive(qPcm, &block, pdMS_TO_TICKS(10)) == pdTRUE && block)
            {
                size_t copy = min(TARGET_SAMPLES - filled, (size_t)I2S_READ_LEN);
                memcpy(win + filled, block, copy * sizeof(int16_t));
                filled += copy;
                releasePcmBlock(block);
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(2));
                uint32_t nowWait = millis();
                if (nowWait - lastIdleLog > 1000)
                {
                    LOGD("[AI] waiting for PCM data (%u/%u)", (unsigned)filled, (unsigned)TARGET_SAMPLES);
                    lastIdleLog = nowWait;
                }
            }
            esp_task_wdt_reset();
        }
        LOGD("[AI] running inference on %u samples", (unsigned)TARGET_SAMPLES);
        float prob = tflm_infer_prob(win, TARGET_SAMPLES);
        g_lastProb = prob;
        bool state = detector.update(prob);
        g_lastScore = detector.score();
        g_isCrying = state;
        LOGI("[AI] prob=%.3f score=%.3f state=%s (night=%s)\n",
             prob, g_lastScore, state ? "CRY" : "CALM", nightMode ? "ON" : "OFF");

        uint32_t now = millis();
        if (state != prevState)
        {
            GpsFix fix = gps_get_fix();
            g_lastLat = fix.lat;
            g_lastLng = fix.lng;
            g_gpsValid = fix.valid;
            updateCryLed(state);
            if (nightMode)
            {
                speaker_enqueue(SpeakCmd::Ting);
            }
            else
            {
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
            if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(50)) != pdTRUE)
            {
                Serial.println("Event queue full, dropping cry event");
            }
            prevState = state;
        }
        else
        {
            if (state && (now - g_lastCryReminderMs) > REMIND_INTERVAL_MS)
            {
                g_lastCryReminderMs = now;
                Serial.println("[AI] Cry reminder trigger, still detecting cry state");
                if (nightMode)
                {
                    speaker_enqueue(SpeakCmd::Ting);
                }
                else
                {
                    speaker_enqueue(SpeakCmd::Cry);
                }
            }
            if (state && (now - g_lastHeartbeatReportMs) > CRY_HEARTBEAT_INTERVAL_MS)
            {
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
                if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(10)) != pdTRUE)
                {
                    Serial.println("Heartbeat queue full, dropping cry heartbeat");
                }
                else
                {
                    g_lastHeartbeatReportMs = now;
                    LOGD("[AI] queued heartbeat event");
                }
            }
        }
        filled = 0;
        esp_task_wdt_reset();
        vTaskDelay(delayTicks);
    }
}

// ===== GPS Task =====// ===== GPS Task =====
static void taskGps(void *arg)
{
    uint32_t lastLog = 0;
    for (;;)
    {
        gps_loop();
        if (millis() - lastLog > 10000)
        {
            GpsFix fix = gps_get_fix();
            if (fix.valid)
            {
                Serial.printf("[GPS] lat=%.5f lng=%.5f tuổi=%lu ms\n", fix.lat, fix.lng, fix.age_ms);
            }
            else
            {
                Serial.println("[GPS] Đang tìm vệ tinh...");
            }
            lastLog = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}


static void taskSender(void *arg)
{
    CryEvent evt;
    for (;;)
    {
        if (xQueueReceive(qEvents, &evt, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        while (!wifi_ensure_connected(15000))
        {
            LOGW("[Send] WiFi chưa sẵn sàng, chờ gửi sự kiện...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        String json = "{";
        json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
        const char *evtName = evt.heartbeat ? "cry_heartbeat" : (evt.crying ? "cry_on" : "cry_off");
        json += "\"event\":\"" + String(evtName) + "\",";
        json += "\"prob\":" + String(evt.prob, 3) + ",";
        json += "\"score\":" + String(evt.score, 3) + ",";
        json += "\"lat\":" + String(evt.lat, 6) + ",";
        json += "\"lng\":" + String(evt.lng, 6) + ",";
        json += "\"gps_valid\":";
        json += (evt.gpsValid ? "true" : "false");
        json += ",";
        json += "\"duration_ms\":" + String(evt.durationMs) + ",";
        json += "\"ts\":" + String(evt.tsMs);
        json += "}";
        LOGI("[Send] event=%s prob=%.2f score=%.2f duration=%lu\n",
             evtName, evt.prob, evt.score, static_cast<unsigned long>(evt.durationMs));
        bool sent = false;
        for (uint8_t attempt = 0; attempt < 3 && !sent; ++attempt)
        {
            if (RestClient::postJSON(BACKEND_URL, json))
            {
                sent = true;
                LOGD("[Send] event delivered on attempt %u", attempt + 1);
            }
            else
            {
                LOGW("[Send] Gửi sự kiện thất bại lần %u", attempt + 1);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        if (!sent)
        {
            LOGE("[Send] Bỏ sự kiện khóc sau khi thử nhiều lần");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void initRuntime()
{
    Serial.begin(115200);
    delay(200);
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("WiFiGeneric", ESP_LOG_NONE);
    esp_log_level_set("WiFi", ESP_LOG_NONE);
    esp_task_wdt_init(5, true);
    wifi_service_init();
    applyDetectorProfile();
    setStatusMessage("Đang khởi động hệ thống...");
    LOGI("[Init] Runtime initialized\n");
}
static void initGpioAndStatus()
{
    pinMode(LED_WIFI_PIN, OUTPUT);
    pinMode(LED_CRY_RED_PIN, OUTPUT);
    pinMode(LED_CRY_GREEN_PIN, OUTPUT);
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    wifi_update_led();
    updateCryLed(false);
}

static void initQueues()
{
    qPcm = xQueueCreate(PCM_POOL_BUFFERS, sizeof(int16_t *));
    qEvents = xQueueCreate(6, sizeof(CryEvent));
#if USE_MAX98357A_SPK
    qSpeaker = xQueueCreate(4, sizeof(SpeakCmd));
#endif
    if (!qPcm || !qEvents)
    {
        Serial.println("[Init] Failed to allocate queues");
    }
    if (!initPcmPool())
    {
        Serial.println("[Init] Failed to init PCM buffer pool");
    }
    if (!ensure_infer_window())
    {
        Serial.println("[Init] Inference buffer not ready yet, will retry in task");
    }
    LOGI("[Init] Queues ready (PCM=%u buffers, events=%u slots)\n",
         g_pcmBuffersAllocated, uxQueueSpacesAvailable(qEvents));
}

static void startTasks()
{
    wifi_service_start();
    xTaskCreatePinnedToCore(taskMic, "mic", 3072, nullptr, 2, &hMicTask, 0);
    xTaskCreatePinnedToCore(taskInfer, "infer", 7168, nullptr, 3, &hInferTask, 1);
    xTaskCreatePinnedToCore(taskGps, "gps", 2048, nullptr, 1, &hGpsTask, 1);
    xTaskCreatePinnedToCore(taskSender, "sender", 3072, nullptr, 1, &hSendTask, 1);
#if USE_MAX98357A_SPK
    if (qSpeaker)
    {
        xTaskCreatePinnedToCore(taskSpeaker, "speaker", 4096, nullptr, 1, nullptr, 1);
    }
#endif
}

static void handleModeButton(uint32_t now)
{
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
    if (rawNow != rawPrev)
    {
        rawPrev = rawNow;
        lastChangeMs = now;
    }
    if ((now - lastChangeMs) > DEBOUNCE_MS && stableState != rawPrev)
    {
        stableState = rawPrev;
        if (!stableState)
        {
            pressStartMs = now;
        }
        else
        {
            if (pressStartMs > 0)
            {
                uint32_t pressDur = now - pressStartMs;
                bool canToggle = (now > IGNORE_AFTER_RESET_MS) &&
                                 (pressDur >= MIN_PRESS_MS && pressDur <= MAX_PRESS_MS) &&
                                 ((now - lastModeToggleMs) > TOGGLE_GAP_MS);
                if (canToggle)
                {
                    nightMode = !nightMode;
                    lastModeToggleMs = now;
                    Serial.printf("[MODE] nightMode=%s\n", nightMode ? "ON" : "OFF");
                    applyDetectorProfile();
                    wifi_blink_led(nightMode ? 2 : 1);
                    speaker_enqueue(nightMode ? SpeakCmd::NightOn : SpeakCmd::NightOff, true);
                }
            }
            pressStartMs = 0;
        }
    }
}

void setup()
{
    initRuntime();
    initGpioAndStatus();
    api_begin();
    i2s_init();
    gps_begin();
    initQueues();
    startTasks();
    setStatusMessage("Hệ thống đang nghe âm thanh...");
}
void loop()
{
    uint32_t now = millis();
    handleModeButton(now);

    api_loop();
    delay(5);
}

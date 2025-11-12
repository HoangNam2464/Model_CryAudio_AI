#include <Arduino.h>
#include <WiFi.h>
#include "driver/i2s.h"
#include <esp_heap_caps.h>

#include "Config.h"
#include "api_server.h"
#include <CryDetector.h>
#include <RestClient.h>
#include <gps.h>
#include <tflm_infer.h>

// ===== Globals exposed to API =====
float g_lastProb  = 0.0f;
float g_lastScore = 0.0f;
bool  g_isCrying  = false;
double g_lastLat  = 0.0;
double g_lastLng  = 0.0;
bool   g_gpsValid = false;

// ===== FreeRTOS handles =====
static TaskHandle_t hMicTask    = nullptr;
static TaskHandle_t hInferTask  = nullptr;
static TaskHandle_t hGpsTask    = nullptr;
static TaskHandle_t hSendTask   = nullptr;
static QueueHandle_t qPcm       = nullptr;
static QueueHandle_t qEvents    = nullptr;

struct CryEvent {
    bool crying;
    float prob;
    float score;
    double lat;
    double lng;
    bool gpsValid;
    uint32_t tsMs;
};

// ===== Cry debouncer =====
static CryDetector detector(
    0.70f, 0.18f, 0.15f,
    1.0f, 2.3f,
    2.0f, 1.5f,
    0.25f,
    INFER_INTERVAL_S
);

// ===== I2S setup =====
static void i2s_init() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = I2S_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMP;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 512;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_SCK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
    pins.data_out_num = -1;
    pins.data_in_num  = I2S_SD_PIN;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_set_clk(I2S_NUM_0, I2S_SAMPLE_RATE, (i2s_bits_per_sample_t)I2S_BITS_PER_SAMP, I2S_CHANNEL_MONO);
}

// ===== MIC Task: read PCM blocks =====
static void taskMic(void* arg) {
    const size_t CHUNK = I2S_READ_LEN;
    size_t bytes_read = 0;
    for(;;){
        int16_t* block = static_cast<int16_t*>(heap_caps_malloc(CHUNK * sizeof(int16_t), MALLOC_CAP_8BIT));
        if (!block){
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        esp_err_t err = i2s_read(I2S_NUM_0, block, CHUNK * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (err != ESP_OK || bytes_read != CHUNK * sizeof(int16_t)) {
            heap_caps_free(block);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (xQueueSend(qPcm, &block, 0) != pdTRUE) {
            heap_caps_free(block);
        }
    }
}

// ===== Infer Task =====
static void taskInfer(void* arg) {
    const TickType_t delayTicks = (TickType_t)(INFER_INTERVAL_S*1000)/portTICK_PERIOD_MS;
    const size_t TARGET_SAMPLES = (size_t)(I2S_SAMPLE_RATE * INFER_INTERVAL_S);
    int16_t* win = static_cast<int16_t*>(heap_caps_malloc(TARGET_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!win) {
        Serial.println("Failed to allocate inference window buffer");
        vTaskDelete(nullptr);
        return;
    }
    size_t filled = 0;
    bool prevState=false;
    tflm_begin();

    for(;;){
        while (filled < TARGET_SAMPLES){
            int16_t* block=nullptr;
            if (xQueueReceive(qPcm, &block, pdMS_TO_TICKS(10)) == pdTRUE && block){
                size_t copy = min(TARGET_SAMPLES - filled, (size_t)I2S_READ_LEN);
                memcpy(win + filled, block, copy*sizeof(int16_t));
                filled += copy;
                heap_caps_free(block);
            } else {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        float prob = tflm_infer_prob(win, TARGET_SAMPLES);
        g_lastProb = prob;
        bool state = detector.update(prob);
        g_lastScore = detector.score();
        g_isCrying = state;

        if (state != prevState){
            GpsFix fix = gps_get_fix();
            g_lastLat = fix.lat; g_lastLng = fix.lng; g_gpsValid = fix.valid;
            CryEvent evt{};
            evt.crying = state;
            evt.prob = prob;
            evt.score = g_lastScore;
            evt.lat = g_lastLat;
            evt.lng = g_lastLng;
            evt.gpsValid = g_gpsValid;
            evt.tsMs = millis();
            if (xQueueSend(qEvents, &evt, pdMS_TO_TICKS(50)) != pdTRUE) {
                Serial.println("Event queue full, dropping cry event");
            }
            prevState = state;
        }
        filled = 0;
        vTaskDelay(delayTicks);
    }
}

// ===== GPS Task =====
static void taskGps(void* arg){
    for(;;){
        gps_loop();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static bool wifi_connect_blocking(){
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0=millis();
    while (WiFi.status()!=WL_CONNECTED && millis()-t0<20000){
        delay(300);
    }
    return WiFi.status()==WL_CONNECTED;
}

static bool ensure_wifi(){
    if (WiFi.status()==WL_CONNECTED) return true;
    return wifi_connect_blocking();
}

static void taskSender(void* arg){
    CryEvent evt;
    for(;;){
        if (xQueueReceive(qEvents, &evt, portMAX_DELAY) != pdTRUE){
            continue;
        }
        if (!ensure_wifi()){
            Serial.println("WiFi unavailable, event skipped");
            continue;
        }
        String json = "{";
        json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
        json += "\"event\":\"" + String(evt.crying ? "cry_on" : "cry_off") + "\",";
        json += "\"prob\":" + String(evt.prob, 3) + ",";
        json += "\"score\":" + String(evt.score, 3) + ",";
        json += "\"lat\":" + String(evt.lat, 6) + ",";
        json += "\"lng\":" + String(evt.lng, 6) + ",";
        json += "\"gps_valid\":"; json += (evt.gpsValid ? "true" : "false"); json += ",";
        json += "\"ts\":" + String(evt.tsMs);
        json += "}";
        if (!RestClient::postJSON(BACKEND_URL, json)){
            Serial.println("Failed to post cry event");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setup(){
    Serial.begin(115200); delay(200);
    wifi_connect_blocking();
    api_begin();
    i2s_init();
    gps_begin();

    qPcm = xQueueCreate(12, sizeof(int16_t*));
    qEvents = xQueueCreate(6, sizeof(CryEvent));
    xTaskCreatePinnedToCore(taskMic, "mic", 4096, nullptr, 2, &hMicTask, 0);
    xTaskCreatePinnedToCore(taskInfer, "infer", 8192, nullptr, 3, &hInferTask, 1);
    xTaskCreatePinnedToCore(taskGps, "gps", 3072, nullptr, 1, &hGpsTask, 1);
    xTaskCreatePinnedToCore(taskSender, "sender", 4096, nullptr, 1, &hSendTask, 1);
}

void loop(){
    static uint32_t lastCheck=0;
    if (millis()-lastCheck>5000){
        if (WiFi.status()!=WL_CONNECTED) wifi_connect_blocking();
        lastCheck=millis();
    }
    api_loop();
    delay(5);
}

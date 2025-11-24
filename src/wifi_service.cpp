#include "wifi_service.h"

#include "Config.h"
#include "WifiConfig.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <algorithm>

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

// Hàm do main.cpp cung cấp, dùng để hiển thị trạng thái ra API
extern void setStatusMessage(const char *msg);

static EventGroupHandle_t g_wifiEventGroup = nullptr;
static constexpr EventBits_t WIFI_READY_BIT = BIT0;
static constexpr uint32_t WIFI_BACKOFF_MIN_MS = 1000;
static constexpr uint32_t WIFI_BACKOFF_MAX_MS = 10000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static bool g_setupApActive = false;
static String g_setupApSsid;
static bool g_wifiReconnectRequest = false;
static constexpr const char *CONFIG_AP_PASS = "12345678";
static WiFiEventId_t g_wifiEventId = 0;
static uint8_t g_wifiFailCount = 0;
static uint8_t g_lastWifiReason = WIFI_REASON_UNSPECIFIED;
static bool g_wifiConnected = false;
static bool g_wifiPausedAfterFail = false;
static TaskHandle_t g_wifiTask = nullptr;

static void updateWifiLed()
{
#if LED_WIFI_ACTIVE_LOW
    digitalWrite(LED_WIFI_PIN, g_wifiConnected ? LOW : HIGH);
#else
    digitalWrite(LED_WIFI_PIN, g_wifiConnected ? HIGH : LOW);
#endif
}

void wifi_update_led()
{
    updateWifiLed();
}

void wifi_blink_led(uint8_t times)
{
    for (uint8_t i = 0; i < times; i++)
    {
        digitalWrite(LED_WIFI_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(80));
        digitalWrite(LED_WIFI_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    updateWifiLed();
}

static String get_setup_portal_url()
{
    if (!g_setupApActive)
        return String();
    IPAddress ip = WiFi.softAPIP();
    return String("http://") + ip.toString() + "/wifi";
}

static void log_setup_portal_link(const char *prefix = nullptr)
{
    if (!g_setupApActive)
        return;
    String link = get_setup_portal_url();
    if (!link.length())
        return;
    if (prefix && *prefix)
    {
        LOGI("[WiFi] %s -> %s\n", prefix, link.c_str());
    }
    else
    {
        LOGI("[WiFi] Setup portal: %s\n", link.c_str());
    }
}

static void ensure_setup_ap()
{
    if (g_setupApActive)
        return;
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
    g_setupApSsid = String("AudioCry-Setup-") + suffix;

    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP_STA); // bật luôn AP+STA để thống nhất hành vi, hạn chế crash lạ
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    bool apOk = WiFi.softAP(g_setupApSsid.c_str(), CONFIG_AP_PASS, 6, 0, 4);
    if (apOk)
    {
        Serial.printf("[WiFi] Setup AP enabled: %s / %s\n", g_setupApSsid.c_str(), CONFIG_AP_PASS);
        g_setupApActive = true;
        log_setup_portal_link("Open setup portal");
    }
    else
    {
        Serial.println("[WiFi] Failed to start setup AP");
    }
}

static void stop_setup_ap()
{
    if (!g_setupApActive)
        return;
    WiFi.softAPdisconnect(true);
    g_setupApActive = false;
}

static void wifi_mark_connected()
{
    g_wifiConnected = true;
    updateWifiLed();
    if (WiFi.status() == WL_CONNECTED)
    {
        LOGI("[WiFi] Link active: SSID=%s IP=%s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
    else
    {
        LOGI("[WiFi] Link active\n");
    }
    if (g_wifiEventGroup)
    {
        xEventGroupSetBits(g_wifiEventGroup, WIFI_READY_BIT);
    }
}

static void wifi_mark_disconnected()
{
    g_wifiConnected = false;
    updateWifiLed();
    ensure_setup_ap();
    log_setup_portal_link("WiFi not connected");
    LOGW("[WiFi] Link inactive (reason=%u)", g_lastWifiReason);
    if (g_wifiEventGroup)
    {
        xEventGroupClearBits(g_wifiEventGroup, WIFI_READY_BIT);
    }
}

static const char *wifi_reason_to_text(uint8_t reason)
{
    switch (reason)
    {
    case WIFI_REASON_NO_AP_FOUND:
        return "không tìm thấy SSID";
    case WIFI_REASON_AUTH_FAIL:
        return "sai mật khẩu";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "mất tín hiệu AP";
    case WIFI_REASON_ASSOC_LEAVE:
        return "AP ngắt kết nối";
    default:
        return "lý do khác";
    }
}

static void handle_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOGI("[WiFi] Đã kết nối tới AP %s\n", WiFi.SSID().c_str());
        wifi_mark_connected();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        g_lastWifiReason = info.wifi_sta_disconnected.reason;
        if (info.wifi_sta_disconnected.reason != WIFI_REASON_NO_AP_FOUND)
        {
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

static bool wifi_connect_blocking()
{
    if (g_wifiPausedAfterFail)
    {
        LOGW("[WiFi] Reconnect paused until new config.");
        setStatusMessage("Đang dùng AP cấu hình, vui lòng nhập WiFi mới.");
        ensure_setup_ap();
        wifi_mark_disconnected();
        return false;
    }
    WifiCredentials creds;
    wifi_config_load(creds);
    bool usingDefaults = false;
    // Ưu tiên SSID/PASS hardcode để test, thay thế luôn nếu được cấu hình trong Config.h
    if (strlen(WIFI_SSID) > 0)
    {
        creds.ssid = WIFI_SSID;
        creds.pass = WIFI_PASS;
        usingDefaults = true;
    }
    if (creds.ssid.isEmpty())
    {
        LOGW("[WiFi] No saved WiFi, keeping setup AP active.");
        setStatusMessage("Chưa cấu hình WiFi, kết nối AP để nhập.");
        ensure_setup_ap();
        log_setup_portal_link("Nhập WiFi tại");
        g_wifiPausedAfterFail = true;
        wifi_mark_disconnected();
        return false;
    }
    LOGI("[WiFi] Đang kết nối tới \"%s\" (len pass=%u)%s\n",
         creds.ssid.c_str(), (unsigned)creds.pass.length(),
         usingDefaults ? " [Config.h]" : "");
    if (g_wifiEventId == 0)
    {
        g_wifiEventId = WiFi.onEvent(handle_wifi_event);
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoConnect(true);
    WiFi.setSleep(false);
    LOGD("[WiFi] WiFi.mode(AP_STA) set, sleep=OFF");
    vTaskDelay(pdMS_TO_TICKS(50)); // cho driver ổn định trước khi begin
    setStatusMessage("Đang chờ kết nối WiFi...");
    WiFi.begin(creds.ssid.c_str(), creds.pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok)
    {
        stop_setup_ap();
        LOGI("[WiFi] Kết nối thành công, IP: %s\n", WiFi.localIP().toString().c_str());
        String cfgUrl = String("http://") + WiFi.localIP().toString() + "/wifi";
        LOGI("[WiFi] Trang cấu hình: %s\n", cfgUrl.c_str());
        setStatusMessage("WiFi đã kết nối, đang khởi tạo...");
        g_wifiFailCount = 0;
        g_wifiPausedAfterFail = false;
        wifi_mark_connected();
    }
    else
    {
        g_wifiFailCount = std::min<uint8_t>(g_wifiFailCount + 1, 10);
        LOGW("[WiFi] Kết nối thất bại (reason=%s).\n", wifi_reason_to_text(g_lastWifiReason));
        wifi_mark_disconnected();
        ensure_setup_ap();
        if (!g_wifiPausedAfterFail)
        {
            g_wifiPausedAfterFail = true;
            LOGW("[WiFi] Tạm ngừng thử lại, chờ nhập WiFi mới hoặc reboot.");
        }
        setStatusMessage("Không kết nối được, vui lòng dùng AP cấu hình.");
    }
    return ok;
}

bool wifi_ensure_connected(uint32_t waitMs)
{
    if (!g_wifiEventGroup)
    {
        return WiFi.status() == WL_CONNECTED;
    }
    EventBits_t bits = xEventGroupWaitBits(
        g_wifiEventGroup,
        WIFI_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(waitMs));
    return (bits & WIFI_READY_BIT);
}

void wifi_request_reconnect()
{
    g_wifiPausedAfterFail = false;
    g_wifiReconnectRequest = true;
    wifi_mark_disconnected();
    ensure_setup_ap();
    log_setup_portal_link("Reconnect via");
    String portal = get_setup_portal_url();
    if (portal.length())
    {
        String msg = String("Mở ") + portal + " để kết nối lại.";
        setStatusMessage(msg.c_str());
    }
}

bool wifi_is_setup_ap_active()
{
    return g_setupApActive;
}

const char *wifi_get_setup_ap_ssid()
{
    return g_setupApSsid.c_str();
}

const char *wifi_get_setup_ap_pass()
{
    return CONFIG_AP_PASS;
}

static void taskWifi(void *)
{
    uint32_t backoffMs = WIFI_BACKOFF_MIN_MS;
    LOGI("[WiFiTask] start\n");
    for (;;)
    {
        if (g_wifiReconnectRequest)
        {
            g_wifiReconnectRequest = false;
            g_wifiPausedAfterFail = false;
            WiFi.disconnect(true, true);
            wifi_mark_disconnected();
            backoffMs = WIFI_BACKOFF_MIN_MS;
        }
        if (g_wifiPausedAfterFail)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (WiFi.status() != WL_CONNECTED)
        {
            bool ok = wifi_connect_blocking();
            if (ok)
            {
                backoffMs = WIFI_BACKOFF_MIN_MS;
                LOGI("[WiFiTask] Connected, sleeping 200ms\n");
            }
            else
            {
                backoffMs = std::min(backoffMs * 2, WIFI_BACKOFF_MAX_MS);
                LOGW("[WiFiTask] Retry scheduled in %u ms", backoffMs);
                vTaskDelay(pdMS_TO_TICKS(backoffMs));
                continue;
            }
        }
        static uint32_t lastLog = 0;
        uint32_t now = millis();
        if (now - lastLog > 5000) // log tối đa mỗi 5 giây
        {
            LOGD("[WiFiTask] Link healthy");
            lastLog = now;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void wifi_service_init()
{
    if (!g_wifiEventGroup)
    {
        g_wifiEventGroup = xEventGroupCreate();
        if (!g_wifiEventGroup)
        {
            Serial.println("[WiFi] Failed to create event group");
        }
    }
    wifi_config_init();
    ensure_setup_ap();
}

void wifi_service_start()
{
    g_wifiReconnectRequest = true;
    if (g_wifiTask)
    {
        vTaskDelete(g_wifiTask);
        g_wifiTask = nullptr;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 2, &g_wifiTask, 0);
    if (ok != pdPASS)
    {
        Serial.printf("[WiFi] Failed to start wifi task on core0 (heap=%u)\n", (unsigned)esp_get_free_heap_size());
        ok = xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 2, &g_wifiTask, 1);
        if (ok != pdPASS)
        {
            Serial.printf("[WiFi] Failed to start wifi task on core1 (heap=%u)\n", (unsigned)esp_get_free_heap_size());
            g_wifiTask = nullptr;
        }
    }
}

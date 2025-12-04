#include "wifi_service.h"

#include "Config.h"
#include "WifiConfig.h"
#include "log.h"
#include "AppState.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

extern void onWifiConnected();
extern HardwareSerial Serial0;
extern void setStatusMessage(const char *msg);

#ifndef LED_WIFI_ACTIVE_LOW
#define LED_WIFI_ACTIVE_LOW 0
#endif

static EventGroupHandle_t g_wifiEventGroup = nullptr;
static constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
static constexpr EventBits_t WIFI_FAIL_BIT = BIT1;
static constexpr EventBits_t WIFI_READY_BIT = WIFI_CONNECTED_BIT;
static constexpr uint32_t WIFI_BACKOFF_MIN_MS = 1000;
// TĂNG MAX BACKOFF LÊN 60 GIÂY ĐỂ ĐỠ SPAM RECONNECT
static constexpr uint32_t WIFI_BACKOFF_MAX_MS = 60000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr const char *CONFIG_AP_PASS = "";
static bool g_setupApActive = false;
static String g_setupApSsid;
static WiFiEventId_t g_wifiEventId = 0;
static uint8_t g_lastWifiReason = WIFI_REASON_UNSPECIFIED;
static bool g_wifiConnected = false;
static TaskHandle_t g_wifiTask = nullptr;
static bool g_wifiReconnectRequest = false;
static bool g_noCredPause = false;
static uint8_t g_authFailCount = 0;
static uint8_t g_connectFailCount = 0; // consecutive connect failures (any reason)

static void updateWifiLed()
{
    pinMode(LED_WIFI_PIN, OUTPUT);
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
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

    bool apOk = WiFi.softAP(g_setupApSsid.c_str(), CONFIG_AP_PASS, 1, 0, 8);
    if (apOk)
    {
        LOGI("[WiFi] Setup AP enabled: %s / %s\n", g_setupApSsid.c_str(), CONFIG_AP_PASS);
        g_setupApActive = true;
        log_setup_portal_link("Open setup portal");
    }
    else
    {
        LOGE("[WiFi] Failed to start setup AP");
    }
}

static void stop_setup_ap()
{
    if (!g_setupApActive)
        return;
    WiFi.softAPdisconnect(true);
    g_setupApActive = false;
    LOGI("[WiFi] Setup AP stopped");
}

static const char *wifi_reason_to_text(uint8_t reason)
{
    switch (reason)
    {
    case WIFI_REASON_NO_AP_FOUND:
        return "khong tim thay SSID (chi ho tro 2.4GHz)";
    case WIFI_REASON_AUTH_FAIL:
        return "sai mat khau hoac AP chan ket noi";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "mat tin hieu AP";
    case WIFI_REASON_ASSOC_LEAVE:
        return "AP ngat ket noi";
    default:
        return "ly do khac";
    }
}

static void wifi_mark_connected()
{
    bool firstTime = !g_wifiConnected;

    g_wifiConnected = true;
    g_authFailCount = 0;
    g_connectFailCount = 0;
    stop_setup_ap();
    updateWifiLed();

    if (WiFi.status() == WL_CONNECTED)
    {
        LOGI("[WiFi] Link active: SSID=%s IP=%s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        LOGI("[WiFi] Portal: http://%s/wifi\n", WiFi.localIP().toString().c_str());
        app_state_set_wifi(true, WiFi.RSSI(), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
    else
    {
        LOGI("[WiFi] Link active\n");
        app_state_set_wifi(true, 0, "", "");
    }

    if (firstTime)
    {
        onWifiConnected();
    }

    if (g_wifiEventGroup)
    {
        xEventGroupClearBits(g_wifiEventGroup, WIFI_FAIL_BIT);
        xEventGroupSetBits(g_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_READY_BIT);
    }
}

static void wifi_mark_disconnected()
{
    g_wifiConnected = false;
    updateWifiLed();
    app_state_set_wifi(false, 0, "", "");

    if (g_wifiEventGroup)
    {
        xEventGroupClearBits(g_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_READY_BIT);
        xEventGroupSetBits(g_wifiEventGroup, WIFI_FAIL_BIT);
    }

    static uint32_t lastLogMs = 0;
    static uint8_t lastReason = 0xFF;
    uint32_t now = millis();
    if (now - lastLogMs > 8000 || g_lastWifiReason != lastReason)
    {
        LOGW("[WiFi] Link inactive (reason=%u - %s)\n", g_lastWifiReason, wifi_reason_to_text(g_lastWifiReason));
        lastLogMs = now;
        lastReason = g_lastWifiReason;
    }
}

static void handle_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOGI("[WiFi] Connected to AP %s\n", WiFi.SSID().c_str());
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        g_lastWifiReason = WIFI_REASON_UNSPECIFIED;
        wifi_mark_connected();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        g_lastWifiReason = info.wifi_sta_disconnected.reason;
        if (g_lastWifiReason == WIFI_REASON_AUTH_FAIL)
        {
            g_authFailCount = std::min<uint8_t>(g_authFailCount + 1, 10);
        }
        else
        {
            g_authFailCount = 0;
        }
        wifi_mark_disconnected();
        break;
    default:
        break;
    }
}

static bool wifi_connect_once(const WifiCredentials &creds, bool hasStoredCred)
{
    if (creds.ssid.isEmpty())
    {
        return false;
    }

    if (g_wifiEventId == 0)
    {
        g_wifiEventId = WiFi.onEvent(handle_wifi_event);
    }

    if (g_wifiEventGroup)
    {
        xEventGroupClearBits(g_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }

    g_lastWifiReason = WIFI_REASON_UNSPECIFIED;
    WiFi.persistent(false);
    // NHẸ NHÀNG HƠN: KHÔNG ERASE CONFIG MỖI LẦN CONNECT
    WiFi.disconnect(false, false);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(false);
    WiFi.setAutoConnect(false);
    WiFi.mode(g_setupApActive ? WIFI_AP_STA : WIFI_STA);

    LOGI("[WiFi] Connecting to SSID=%s\n", creds.ssid.c_str());
    setStatusMessage("Dang ket noi WiFi...");
    WiFi.begin(creds.ssid.c_str(), creds.pass.c_str());

    bool ok = false;
    if (g_wifiEventGroup)
    {
        EventBits_t bits = xEventGroupWaitBits(
            g_wifiEventGroup,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
        ok = (bits & WIFI_CONNECTED_BIT);
    }
    else
    {
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS)
        {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        ok = WiFi.status() == WL_CONNECTED;
    }

    if (ok)
    {
        if (!g_wifiConnected)
        {
            wifi_mark_connected();
        }
        setStatusMessage("WiFi da ket noi");
        return true;
    }

    bool authFail = (g_lastWifiReason == WIFI_REASON_AUTH_FAIL);
    if (authFail)
    {
        LOGW("[WiFi] Auth failed (%u/3)\n", g_authFailCount);
        if (g_authFailCount >= 3)
        {
            LOGW("[WiFi] Clearing stored WiFi after 3 consecutive auth failures");
            wifi_config_clear();
            g_authFailCount = 0;
            setStatusMessage("Sai mat khau 3 lan, da xoa WiFi.");
            ensure_setup_ap();
            log_setup_portal_link("Nhap WiFi tai");
        }
        else
        {
            setStatusMessage("Sai mat khau, kiem tra lai.");
        }
    }
    else
    {
        g_authFailCount = 0;
        setStatusMessage("WiFi bi mat ket noi, se thu lai...");
    }

    // After repeated failures (lost AP, moved router, etc.) start a setup AP so user can enter a new network.
    g_connectFailCount = std::min<uint8_t>(g_connectFailCount + 1, 100);
    bool shouldStartPortal = !g_setupApActive &&
                             (g_connectFailCount >= 2 ||
                              g_lastWifiReason == WIFI_REASON_NO_AP_FOUND ||
                              g_lastWifiReason == WIFI_REASON_BEACON_TIMEOUT);

    if (shouldStartPortal)
    {
        setStatusMessage("Khong ket noi WiFi cu, mo AP de cau hinh moi.");
        ensure_setup_ap();
        log_setup_portal_link("Mo AP cau hinh WiFi");
    }

    if (!hasStoredCred && !g_setupApActive)
    {
        ensure_setup_ap();
        log_setup_portal_link("Nhap WiFi tai");
    }
    return false;
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
    g_wifiReconnectRequest = true;
    g_noCredPause = false;
    wifi_mark_disconnected();
}

void wifi_clear_no_cred_pause()
{
    g_noCredPause = false;
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
            backoffMs = WIFI_BACKOFF_MIN_MS;
            g_authFailCount = 0;
            // QUAN TRỌNG: KHÔNG DÙNG disconnect(true,true) NỮA ĐỂ TRÁNH CRASH DRIVER
            WiFi.disconnect(false, false);
            wifi_mark_disconnected();
            vTaskDelay(pdMS_TO_TICKS(200)); // cho driver có thời gian settle
        }

        WifiCredentials creds;
        wifi_config_load(creds);
        bool hasStoredCred = wifi_config_has_credentials();
        bool hasAnyCred = creds.ssid.length() > 0;

        if (!hasAnyCred)
        {
            if (!g_noCredPause)
            {
                setStatusMessage("Chua cau hinh WiFi, ket noi AP de nhap.");
            }
            g_noCredPause = true;
            ensure_setup_ap();
            log_setup_portal_link("Setup portal");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        g_noCredPause = false;

        if (WiFi.status() != WL_CONNECTED)
        {
            bool ok = wifi_connect_once(creds, hasStoredCred);
            if (ok)
            {
                backoffMs = WIFI_BACKOFF_MIN_MS;
            }
            else
            {
                backoffMs = std::min<uint32_t>(backoffMs * 2, WIFI_BACKOFF_MAX_MS);
                LOGW("[WiFiTask] Retry scheduled in %u ms\n", (unsigned)backoffMs);
                vTaskDelay(pdMS_TO_TICKS(backoffMs));
                continue;
            }
        }

        static uint32_t lastLog = 0;
        uint32_t now = millis();
        if (now - lastLog > 5000)
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
            Serial0.println("[WiFi] Failed to create event group");
        }
    }
    wifi_config_init();
    updateWifiLed();
}

void wifi_service_start()
{
    g_wifiReconnectRequest = true;
    if (g_wifiTask)
    {
        vTaskDelete(g_wifiTask);
        g_wifiTask = nullptr;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 4, &g_wifiTask, 0);
    if (ok != pdPASS)
    {
        Serial0.printf("[WiFi] Failed to start wifi task on core0 (heap=%u)\n", (unsigned)esp_get_free_heap_size());
        ok = xTaskCreatePinnedToCore(taskWifi, "wifi", 4096, nullptr, 4, &g_wifiTask, 1);
        if (ok != pdPASS)
        {
            Serial0.printf("[WiFi] Failed to start wifi task on core1 (heap=%u)\n", (unsigned)esp_get_free_heap_size());
            g_wifiTask = nullptr;
        }
    }
}

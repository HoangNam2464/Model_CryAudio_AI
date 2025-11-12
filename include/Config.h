#pragma once

// ===== WiFi =====
#define WIFI_SSID      "Tro 3 Thang"
#define WIFI_PASS      "78787878"

// ===== Backend =====
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""   // e.g. "Bearer <token>" if auth is enabled
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

// ===== I2S Mic (INMP441) - ESP32 DevKit 30-pin =====
//  WS/LRCL -> GPIO25,  SCK/BCLK -> GPIO26,  SD/DOUT -> GPIO34 (input-only)
#define I2S_WS_PIN        25
#define I2S_SCK_PIN       26
#define I2S_SD_PIN        34
#define I2S_SAMPLE_RATE   16000
#define I2S_READ_LEN      1024
#define I2S_BITS_PER_SAMP 16

// Inference cadence (seconds)
#define INFER_INTERVAL_S  2.0f

// ===== GPS NEO-6M (UART2) =====
// GPS TX -> RX2 (GPIO16), GPS RX -> TX2 (GPIO17)
#define GPS_RX_PIN   16
#define GPS_TX_PIN   17
#define GPS_BAUD     9600

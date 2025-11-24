#pragma once

/* ============================================================
 *  Cấu hình WiFi (để trống nếu dùng portal cấu hình)
 * ============================================================ */
#define WIFI_SSID      "Quan"
#define WIFI_PASS      "12345679"

/* ============================================================
 *  Cấu hình Backend
 * ============================================================ */
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""    
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  Micro INMP441 (I2S RX – theo chuẩn AI XiaoZhi)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 *     WS/LRCL  -> GPIO7
 *     BCLK     -> GPIO6
 *     DATA OUT -> GPIO5
 * ============================================================ */
#define I2S_WS_PIN         7
#define I2S_SCK_PIN        6
#define I2S_SD_PIN         5     // Mic Data In
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
// Cửa sổ infer 2s như cấu hình đầy đủ
#define INFER_INTERVAL_S   2.0f

/* ============================================================
 *  Loa MAX98357A I2S (TX)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 *     DIN  -> GPIO4
 *     BCLK -> GPIO15
 *     LRC  -> GPIO16
 * ============================================================ */
#define I2S_SD_OUT_PIN     4      // DIN của MAX98357A
#define SPK_I2S_BCLK_PIN   15
#define SPK_I2S_LRCK_PIN   16

#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

/* ============================================================
 *  GPS NEO-6M (UART1 trên S3)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 * ============================================================ */
#define GPS_RX_PIN    18   // GPS TX -> ESP32-S3 RX1
#define GPS_TX_PIN    17   // GPS RX -> ESP32-S3 TX1
#define GPS_BAUD      9600

/* ============================================================
 *  LED & Nút
 * ============================================================ */
#define LED_WIFI_PIN        2    // LED Wi-Fi
#define LED_CRY_RED_PIN     3
#define LED_CRY_GREEN_PIN   8
#define LED_NIGHT_PIN       10   // LED night mode
#define MODE_BUTTON_PIN     9    // dùng nút rời, kéo lên bằng INPUT_PULLUP, nhấn kéo xuống GND
#define LED_WIFI_ACTIVE_LOW 0    // 0 = active HIGH

/* ============================================================
 *  I2C (OLED / SENSOR)
 * ============================================================ */
#define I2C_SDA_PIN    11   // ESP32-S3 30-pin
#define I2C_SCL_PIN    12
#define I2C_FREQ_HZ    400000

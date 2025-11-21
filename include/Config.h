#pragma once

/* ============================================================
 *  Cấu hình WiFi (để trống nếu dùng portal cấu hình)
 * ============================================================ */
#define WIFI_SSID      ""
#define WIFI_PASS      ""

/* ============================================================
 *  Cấu hình Backend
 * ============================================================ */
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""    
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  Micro INMP441 (I2S RX – theo chuẩn AI XiaoZhi)
 *
 *     WS/LRCL  -> GPIO25
 *     BCLK     -> GPIO26
 *     DATA OUT -> GPIO32
 * ============================================================ */
#define I2S_WS_PIN         25
#define I2S_SCK_PIN        26
#define I2S_SD_PIN         32     // Mic Data In
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   2.0f

/* ============================================================
 *  Loa MAX98357A I2S (TX)
 *
 *     DIN  -> GPIO33
 *     BCLK -> GPIO14
 *     LRC  -> GPIO27
 * ============================================================ */
#define I2S_SD_OUT_PIN     33     // DIN của MAX98357A
#define SPK_I2S_BCLK_PIN   14
#define SPK_I2S_LRCK_PIN   27

#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

/* ============================================================
 *  GPS NEO-6M (UART2)
 * ============================================================ */
#define GPS_RX_PIN    16   // GPS TX -> ESP32 RX2
#define GPS_TX_PIN    17   // GPS RX -> ESP32 TX2
#define GPS_BAUD      9600

/* ============================================================
 *  LED & Nút
 * ============================================================ */
#define LED_WIFI_PIN        2  
#define LED_CRY_RED_PIN     4
#define LED_CRY_GREEN_PIN   5
#define MODE_BUTTON_PIN     13

#ifndef LED_WIFI_ACTIVE_LOW
#define LED_WIFI_ACTIVE_LOW 0  // 0 = active HIGH (đèn tắt khi chưa kết nối STA, sáng khi đã kết nối)
#endif

/* ============================================================
 *  I2C (OLED / SENSOR)
 * ============================================================ */
#define I2C_SDA_PIN    21   // CHUẨN của ESP32
#define I2C_SCL_PIN    22
#define I2C_FREQ_HZ    400000

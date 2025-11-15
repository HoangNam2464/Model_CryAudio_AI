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
#define API_TOKEN      ""          // e.g. "Bearer <token>"
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  Micro INMP441 (I2S0, ESP32 DevKit 30 chân)
 *
 *  Dây đấu thực tế:
 *     WS/LRCL  -> GPIO25 (D25)
 *     BCLK     -> GPIO26 (D26)
 *     DATA OUT -> GPIO22 (D22)
 * ============================================================ */
#define I2S_WS_PIN         25
#define I2S_SCK_PIN        26
#define I2S_SD_PIN         22
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   2.0f

/* ============================================================
 *  GPS NEO-6M (UART2)
 *
 *  Dây đấu thực tế:
 *     GPS TX -> GPIO16 (RX2)
 *     GPS RX -> GPIO17 (TX2)
 * ============================================================ */
#define GPS_RX_PIN    16
#define GPS_TX_PIN    17
#define GPS_BAUD      9600

/* ============================================================
 *  LED trạng thái
 * ============================================================ */
#define LED_WIFI_PIN        2   // LED xanh dương on-board (GPIO2 / D2)
#define LED_CRY_RED_PIN     4   // LED đỏ báo đang khóc (GPIO4 / D4)
#define LED_CRY_GREEN_PIN   5   // LED xanh lá khi im lặng (GPIO5 / D5)

/* ============================================================
 *  I2C (tuỳ chọn cho OLED / cảm biến I2C khác)
 * ============================================================ */
#define I2C_SDA_PIN    19
#define I2C_SCL_PIN    18
#define I2C_FREQ_HZ    400000

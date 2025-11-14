#pragma once

/* ============================================================
 *  WiFi Configuration
 * ============================================================ */
#define WIFI_SSID      ""          // nhập tên WiFi
#define WIFI_PASS      ""          // nhập mật khẩu WiFi

/* ============================================================
 *  Backend Configuration
 * ============================================================ */
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""          // e.g. "Bearer <token>"
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  I2S Microphone – INMP441 (I2S0, ESP32 DevKit 30-pin)
 *
 *  Dây thực tế:
 *     WS/LRCL  → GPIO25 (D25)
 *     BCLK     → GPIO26 (D26)
 *     DATA OUT → GPIO22 (D22)
 * ============================================================ */
#define I2S_WS_PIN         25      // LRCL
#define I2S_SCK_PIN        26      // BCLK
#define I2S_SD_PIN         22      // DOUT từ mic
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   2.0f    // chu kỳ chạy AI (giây)

/* ============================================================
 *  GPS NEO-6M (UART2)
 *
 *  Dây thực tế:
 *     GPS TX → GPIO16 (RX2)
 *     GPS RX → GPIO17 (TX2)
 * ============================================================ */
#define GPS_RX_PIN    16
#define GPS_TX_PIN    17
#define GPS_BAUD      9600

/* ============================================================
 *  LED
 * ============================================================ */
#define LED_PIN        2    // LED on-board (D2)

/* ============================================================
 *  I2C (Tùy chọn – dùng cho OLED hoặc cảm biến I2C)
 *  Nếu bạn KHÔNG dùng OLED thì có thể bỏ qua phần này.
 *
 *  Dây đề xuất:
 *     SDA → GPIO19
 *     SCL → GPIO18
 * ============================================================ */
#define I2C_SDA_PIN    19
#define I2C_SCL_PIN    18
#define I2C_FREQ_HZ    400000

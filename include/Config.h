#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi / Backend giữ nguyên logic cũ
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  MIC INMP441 (I2S RX)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 *  WS -> GPIO4, SCK -> GPIO5, SD -> GPIO6
 * ============================================================ */
#define I2S_WS_PIN         4
#define I2S_SCK_PIN        5
#define I2S_SD_PIN         6     // Mic Data In
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   2.0f

/* ============================================================
 *  SPK MAX98357A (I2S TX)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 *  DIN -> GPIO16, BCLK -> GPIO7, LRC -> GPIO15
 * ============================================================ */
#define I2S_SD_OUT_PIN     16    // DIN của MAX98357A
#define SPK_I2S_BCLK_PIN   7
#define SPK_I2S_LRCK_PIN   15
#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

/* ============================================================
 *  GPS NEO-6M (UART1)
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 * ============================================================ */
#define GPS_RX_PIN         17   // GPS TX -> ESP RX
#define GPS_TX_PIN         18   // GPS RX -> ESP TX
#define GPS_BAUD           9600

/* ============================================================
 *  LED & BUTTONS
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 * ============================================================ */
#define LED_STATUS_PIN     8    // LED xanh dương on-board ESP32-S3 (Wi-Fi/status)
#define BUTTON_DAY_PIN     9    // Nút duy nhất toggle Ngày/Đêm
#define BUTTON_NIGHT_PIN   9    // alias cùng nút (giữ tương thích)
#define LED_WIFI_PIN       LED_STATUS_PIN
#define MODE_BUTTON_PIN    BUTTON_DAY_PIN   // tương thích code cũ
#define LED_WIFI_ACTIVE_LOW 0   // 0 = active HIGH

// Đèn trạng thái bé & chế độ
#define LED_CRY_GREEN_PIN  20   // Bé ngủ yên (xanh lá)
#define LED_CRY_RED_PIN    21   // Bé khóc (đỏ)
#define LED_NIGHT_PIN      38   // Đèn trắng: bật = ban ngày, tắt = ban đêm

/* ============================================================
 *  I2C (tùy chọn)
 * ============================================================ */
#define I2C_SDA_PIN        11
#define I2C_SCL_PIN        12

#endif // CONFIG_H

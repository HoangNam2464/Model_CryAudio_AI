#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi / Backend
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

/* ============================================================
 *  MIC INMP441 (I2S RX) - ESP32-S3 30-PIN
 * ============================================================ */
#define I2S_WS_PIN         4
#define I2S_SCK_PIN        5
#define I2S_SD_PIN         6
#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   2.0f

/* ============================================================
 *  SPK MAX98357A (I2S TX) - ESP32-S3 30-PIN
 * ============================================================ */
#define I2S_SD_OUT_PIN     16
#define SPK_I2S_BCLK_PIN   7
#define SPK_I2S_LRCK_PIN   15
#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

/* ============================================================
 *  GPS NEO-6M (UART1)
 * ============================================================ */
#define GPS_RX_PIN         17   // GPS TX -> ESP RX
#define GPS_TX_PIN         18   // GPS RX -> ESP TX
#define GPS_BAUD           9600

/* ============================================================
 *  LED & BUTTONS - UPDATED SAFE PINS
 * ============================================================ */
#define LED_STATUS_PIN        2
#define LED_WIFI_PIN          LED_STATUS_PIN
#define LED_WIFI_ACTIVE_LOW   0

// ⚠ Nút MODE — KHÔNG dùng GPIO9 (USB-JTAG)
#define BUTTON_DAY_PIN       14
#define BUTTON_NIGHT_PIN     14
#define MODE_BUTTON_PIN      BUTTON_DAY_PIN

// LED trạng thái an toàn (bên phải)
#define LED_CRY_GREEN_PIN    37
#define LED_CRY_RED_PIN      36
#define LED_NIGHT_PIN        38

/* ============================================================
 *  I2C
 * ============================================================ */
#define I2C_SDA_PIN        11
#define I2C_SCL_PIN        12

#endif // CONFIG_H

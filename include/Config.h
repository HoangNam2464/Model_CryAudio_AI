#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Wi-Fi / Backend
// ============================================================
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      "esp32-audiocry"
#define API_TOKEN      ""
#define BACKEND_URL    "http://127.0.0.1:8000/api/cry-events"

// ============================================================
// MIC INMP441 (I2S RX) - ESP32-S3 30-PIN (MAPPING MỚI – PORT 1)
// ============================================================
// SCK (BCLK) = GPIO38
// WS (LRCLK) = GPIO39
// SD (DATA)  = GPIO7
#define I2S_WS_PIN         39
#define I2S_SCK_PIN        38
#define I2S_SD_PIN         7

#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   1.0f

// ============================================================
// SPK MAX98357A (I2S TX – PORT 0) – MAPPING MỚI
// ============================================================
// DIN (Data Out) = GPIO40
// BCLK = GPIO42
// LRCK = GPIO2
#define I2S_SD_OUT_PIN     40
#define SPK_I2S_BCLK_PIN   42
#define SPK_I2S_LRCK_PIN   2

#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

// ============================================================
// GPS NEO-6M (UART2 / Serial2) – MAPPING MỚI
// ============================================================
// GPS TX -> ESP RX16
// GPS RX -> ESP TX17
#define GPS_RX_PIN         16
#define GPS_TX_PIN         17
#define GPS_BAUD           9600

// ============================================================
// LED & BUTTONS
// ============================================================
// Wi-Fi LED: dung LED xanh duong on-board (GPIO8)
#define LED_WIFI_PIN         8
// Night LED (dung rieng hoac dung chung LED_WIFI_PIN neu khong co)
#define LED_NIGHT_PIN        21
#define LED_CRY_RED_PIN      47
#define LED_CRY_GREEN_PIN    48
#define MODE_BUTTON_PIN      14

// ============================================================
// I2C
// ============================================================
#define I2C_SDA_PIN        11
#define I2C_SCL_PIN        12

#endif // CONFIG_H




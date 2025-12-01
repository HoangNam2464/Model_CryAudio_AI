#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      1
#define API_TOKEN      ""
#define BACKEND_URL    "http://192.168.1.110:8000/api/cry-events"

#define I2S_WS_PIN         4
#define I2S_SCK_PIN        5
#define I2S_SD_PIN         6

#define I2S_SAMPLE_RATE    16000
#define I2S_READ_LEN       1024
#define I2S_BITS_PER_SAMP  16
#define INFER_INTERVAL_S   1.0f

#define I2S_SD_OUT_PIN     7
#define SPK_I2S_BCLK_PIN   15
#define SPK_I2S_LRCK_PIN   16

#ifndef USE_MAX98357A_SPK
#define USE_MAX98357A_SPK   1
#endif

#define GPS_RX_PIN         17
#define GPS_TX_PIN         18
#define GPS_BAUD           9600

#define LED_WIFI_PIN         8
#define LED_NIGHT_PIN        21
#define LED_CRY_RED_PIN      47
#define LED_CRY_GREEN_PIN    48
#define MODE_BUTTON_PIN      14

// I2C

#define I2C_SDA_PIN        11
#define I2C_SCL_PIN        12

#endif // CONFIG_H




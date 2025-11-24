#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>

/* ============================================================
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 * ============================================================ */
#define USE_INMP441_MIC        1   // Mic INMP441 I2S RX
#define USE_MAX98357A_SPK      1   // Amp MAX98357A I2S TX
#define USE_GPS_NEO6M          1   // GPS NEO-6M UART
#define USE_OLED_I2C           0   // Mặc định chưa gắn OLED

/* ============================================================
 *  Âm thanh / AI
 * ============================================================ */
#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024
// cửa sổ infer 2s đầy đủ
#define INFER_INTERVAL_S             2.0f

/* ============================================================
 *  INMP441 (I2S RX – theo AI XiaoZhi)
 *
 *  Đấu dây (module): WS/LRCL, BCLK, DOUT
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN:
 *     WS/LRCL  -> GPIO7
 *     BCLK     -> GPIO6
 *     DATA OUT -> GPIO5
 * ============================================================ */
#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        GPIO_NUM_7
#define MIC_I2S_GPIO_SCK       GPIO_NUM_6
#define MIC_I2S_GPIO_DATA_IN   GPIO_NUM_5

/* ============================================================
 *  Loa MAX98357A (I2S TX – theo AI XiaoZhi)
 *
 *  Đấu dây (module): DIN, BCLK, LRC
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN:
 *     DIN  -> GPIO4
 *     BCLK -> GPIO15
 *     LRC  -> GPIO16
 * ============================================================ */
#define SPK_I2S_PORT           I2S_NUM_0
#define SPK_I2S_GPIO_DATA_OUT  GPIO_NUM_4
#define SPK_I2S_GPIO_BCLK      GPIO_NUM_15
#define SPK_I2S_GPIO_LRCLK     GPIO_NUM_16

/* ============================================================
 *  GPS NEO-6M (UART1) – S3 30-pin
 * ============================================================ */
#define GPS_UART_NUM           UART_NUM_1
#define GPS_RX_PIN             GPIO_NUM_18
#define GPS_TX_PIN             GPIO_NUM_17
#define GPS_BAUDRATE           9600

/* ============================================================
 *  LED / Nút
 * ============================================================ */
#define LED_WIFI_GPIO          GPIO_NUM_2
#define LED_CRY_RED_GPIO       GPIO_NUM_3
#define LED_CRY_GREEN_GPIO     GPIO_NUM_8
#define MODE_BUTTON_GPIO       GPIO_NUM_9

/* ============================================================
 *  I2C BUS (tùy chọn)
 * ============================================================ */
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN            GPIO_NUM_21
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN            GPIO_NUM_22
#endif
#define I2C_FREQ_HZ            400000

#endif // _BOARD_CONFIG_H_

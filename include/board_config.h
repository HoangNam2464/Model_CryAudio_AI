#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>

/* ============================================================
 *  Tùy chọn phần cứng
 * ============================================================ */
#define USE_INMP441_MIC        1   // Mic INMP441 I2S RX
#define USE_MAX98357A_SPK      1   // Amp MAX98357A I2S TX
#define USE_GPS_NEO6M          1   // GPS NEO-6M UART2
#define USE_OLED_I2C           0   // Mặc định chưa gắn OLED

/* ============================================================
 *  Âm thanh / AI
 * ============================================================ */
#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024
#define INFER_INTERVAL_S             2.0f

/* ============================================================
 *  INMP441 (I2S0 trên ESP32 DevKit 30 chân)
 *
 *  Đấu dây:
 *     WS/LRCL  -> GPIO25  (D25)
 *     BCLK     -> GPIO26  (D26)
 *     DATA OUT -> GPIO22  (D22)
 * ============================================================ */
#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        GPIO_NUM_25
#define MIC_I2S_GPIO_BCLK      GPIO_NUM_26
#define MIC_I2S_GPIO_DATA_IN   GPIO_NUM_22

/* ============================================================
 *  Loa MAX98357A (I2S0 TX)
 *
 *  Đấu dây:
 *     BCLK -> GPIO26 (chung clock với mic)
 *     LRC  -> GPIO25 (chung WS với mic)
 *     DIN  -> GPIO27 (D27)
 * ============================================================ */
#define SPK_I2S_PORT           I2S_NUM_0
#define SPK_I2S_GPIO_BCLK      GPIO_NUM_26
#define SPK_I2S_GPIO_LRCLK     GPIO_NUM_25
#define SPK_I2S_GPIO_DATA_OUT  GPIO_NUM_27

/* ============================================================
 *  GPS NEO-6M (UART2)
 * ============================================================ */
#define GPS_UART_NUM           UART_NUM_2
#define GPS_RX_PIN             GPIO_NUM_16
#define GPS_TX_PIN             GPIO_NUM_17
#define GPS_BAUDRATE           9600

/* ============================================================
 *  LED / Nút
 * ============================================================ */
#define LED_WIFI_GPIO          GPIO_NUM_2
#define LED_CRY_RED_GPIO       GPIO_NUM_4
#define LED_CRY_GREEN_GPIO     GPIO_NUM_5
#define BOOT_BUTTON_GPIO       GPIO_NUM_0

/* ============================================================
 *  I2C BUS (tùy chọn)
 * ============================================================ */
#define I2C_SDA_PIN            GPIO_NUM_21
#define I2C_SCL_PIN            GPIO_NUM_22
#define I2C_FREQ_HZ            400000

#endif // _BOARD_CONFIG_H_

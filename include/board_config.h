#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>
#include "Config.h"

/* ============================================================
 *  Device configuration mapping for ESP32-S3 AudioCry
 * ============================================================ */
#define USE_INMP441_MIC        1
#define USE_MAX98357A_SPK      1
#define USE_GPS_NEO6M          1
#define USE_OLED_I2C           0

/* ============================================================
 *  Audio / AI
 * ============================================================ */
#define AUDIO_INPUT_SAMPLE_RATE      16000   // Mic 16k
#define AUDIO_OUTPUT_SAMPLE_RATE     24000   // Speaker 24k (MAX98357A)
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024

#ifndef INFER_INTERVAL_S
#define INFER_INTERVAL_S             1.0f
#endif

/* ============================================================
 *  MIC INMP441 (I2S RX) - I2S1 (PORT 1)
 *  WS  = GPIO39, BCLK = GPIO38, SD = GPIO7
 * ============================================================ */
#define MIC_I2S_PORT           I2S_NUM_1
#define MIC_I2S_GPIO_WS        ((gpio_num_t)I2S_WS_PIN)
#define MIC_I2S_GPIO_SCK       ((gpio_num_t)I2S_SCK_PIN)
#define MIC_I2S_GPIO_DATA_IN   ((gpio_num_t)I2S_SD_PIN)
#define I2S_RX_PORT            MIC_I2S_PORT
#define I2S_RX_SAMPLE_RATE     AUDIO_INPUT_SAMPLE_RATE
#define I2S_RX_BCLK_GPIO       MIC_I2S_GPIO_SCK
#define I2S_RX_LRCK_GPIO       MIC_I2S_GPIO_WS
#define I2S_RX_DIN_GPIO        MIC_I2S_GPIO_DATA_IN

/* ============================================================
 *  Speaker MAX98357A (I2S TX) - I2S0 (PORT 0)
 *  DIN = GPIO40, BCLK = GPIO42, LRCK = GPIO2
 * ============================================================ */
#define SPK_I2S_PORT           I2S_NUM_0
#define SPK_I2S_GPIO_DATA_OUT  ((gpio_num_t)I2S_SD_OUT_PIN)
#define SPK_I2S_GPIO_BCLK      ((gpio_num_t)SPK_I2S_BCLK_PIN)
#define SPK_I2S_GPIO_LRCK      ((gpio_num_t)SPK_I2S_LRCK_PIN)
#define I2S_TX_PORT            SPK_I2S_PORT
#define I2S_TX_SAMPLE_RATE     AUDIO_OUTPUT_SAMPLE_RATE
#define I2S_TX_BCLK_GPIO       SPK_I2S_GPIO_BCLK
#define I2S_TX_LRCK_GPIO       SPK_I2S_GPIO_LRCK
#define I2S_TX_DOUT_GPIO       SPK_I2S_GPIO_DATA_OUT

/* ============================================================
 *  GPS NEO-6M (UART2)
 * ============================================================ */
#define GPS_UART_NUM           UART_NUM_2
#define GPS_RX_GPIO            ((gpio_num_t)GPS_RX_PIN)
#define GPS_TX_GPIO            ((gpio_num_t)GPS_TX_PIN)
#define GPS_BAUDRATE           GPS_BAUD

/* ============================================================
 *  LED & Buttons
 * ============================================================ */
#define LED_CRY_RED_GPIO       ((gpio_num_t)LED_CRY_RED_PIN)
#define LED_CRY_GREEN_GPIO     ((gpio_num_t)LED_CRY_GREEN_PIN)
#define LED_NIGHT_GPIO         ((gpio_num_t)LED_NIGHT_PIN)
#define MODE_BUTTON_GPIO       ((gpio_num_t)MODE_BUTTON_PIN)

/* ============================================================
 *  I2C BUS
 * ============================================================ */
#define I2C_SDA_GPIO           ((gpio_num_t)I2C_SDA_PIN)
#define I2C_SCL_GPIO           ((gpio_num_t)I2C_SCL_PIN)
#define I2C_FREQ_HZ            400000

/* ============================================================
 *  NVS keys for audio
 * ============================================================ */
#define NVS_NAMESPACE_AUDIO      "audio_cfg"
#define NVS_KEY_OUTPUT_VOLUME    "vol_out"

#endif // _BOARD_CONFIG_H_

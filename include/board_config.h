#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>
#include "Config.h"

/* ============================================================
 *  ĐÃ CHUYỂN SANG ESP32-S3 30-PIN
 * ============================================================ */
#define USE_INMP441_MIC        1   // Mic INMP441 I2S RX
#define USE_MAX98357A_SPK      1   // Amp MAX98357A I2S TX
#define USE_GPS_NEO6M          1   // GPS NEO-6M UART1
#define USE_OLED_I2C           0   // Chưa gắn OLED

/* ============================================================
 *  Âm thanh / AI
 * ============================================================ */
#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024
#ifndef INFER_INTERVAL_S
#define INFER_INTERVAL_S             2.0f
#endif

/* ============================================================
 *  INMP441 (I2S RX – theo AI XiaoZhi)
 *  WS=GPIO4, BCLK=GPIO5, DATA=GPIO6
 * ============================================================ */
#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        ((gpio_num_t)I2S_WS_PIN)
#define MIC_I2S_GPIO_SCK       ((gpio_num_t)I2S_SCK_PIN)
#define MIC_I2S_GPIO_DATA_IN   ((gpio_num_t)I2S_SD_PIN)

/* ============================================================
 *  Loa MAX98357A (I2S TX – theo AI XiaoZhi)
 *  DIN=GPIO16, BCLK=GPIO7, LRC=GPIO15
 * ============================================================ */
#define SPK_I2S_PORT           I2S_NUM_0
#define SPK_I2S_GPIO_DATA_OUT  ((gpio_num_t)I2S_SD_OUT_PIN)
#define SPK_I2S_GPIO_BCLK      ((gpio_num_t)SPK_I2S_BCLK_PIN)
#define SPK_I2S_GPIO_LRCLK     ((gpio_num_t)SPK_I2S_LRCK_PIN)

/* ============================================================
 *  GPS NEO-6M (UART1)
 * ============================================================ */
#define GPS_UART_NUM           UART_NUM_1
#define GPS_RX_GPIO            ((gpio_num_t)GPS_RX_PIN)   // từ Config.h
#define GPS_TX_GPIO            ((gpio_num_t)GPS_TX_PIN)
#define GPS_BAUDRATE           GPS_BAUD

/* ============================================================
 *  LED / Nút
 * ============================================================ */
#define LED_WIFI_GPIO          ((gpio_num_t)LED_WIFI_PIN)
#define LED_CRY_RED_GPIO       ((gpio_num_t)LED_CRY_RED_PIN)
#define LED_CRY_GREEN_GPIO     ((gpio_num_t)LED_CRY_GREEN_PIN)
#define MODE_BUTTON_GPIO       ((gpio_num_t)MODE_BUTTON_PIN)
#define LED_NIGHT_GPIO         ((gpio_num_t)LED_NIGHT_PIN)

/* ============================================================
 *  I2C BUS (tùy chọn)
 * ============================================================ */
#define I2C_SDA_GPIO           ((gpio_num_t)I2C_SDA_PIN)
#define I2C_SCL_GPIO           ((gpio_num_t)I2C_SCL_PIN)
#define I2C_FREQ_HZ            400000

#endif // _BOARD_CONFIG_H_

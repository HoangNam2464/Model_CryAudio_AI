#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>
#include "Config.h"

#define USE_INMP441_MIC        1
#define USE_MAX98357A_SPK      1
#define USE_GPS_NEO6M          1
#define USE_OLED_I2C           0

#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     24000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024

#ifndef INFER_INTERVAL_S
#define INFER_INTERVAL_S             1.0f
#endif

#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        ((gpio_num_t)I2S_WS_PIN)      // 4
#define MIC_I2S_GPIO_SCK       ((gpio_num_t)I2S_SCK_PIN)     // 5
#define MIC_I2S_GPIO_DATA_IN   ((gpio_num_t)I2S_SD_PIN)      // 6

#define I2S_RX_PORT            MIC_I2S_PORT
#define I2S_RX_SAMPLE_RATE     AUDIO_INPUT_SAMPLE_RATE
#define I2S_RX_BCLK_GPIO       MIC_I2S_GPIO_SCK
#define I2S_RX_LRCK_GPIO       MIC_I2S_GPIO_WS
#define I2S_RX_DIN_GPIO        MIC_I2S_GPIO_DATA_IN

#define SPK_I2S_PORT           I2S_NUM_1
#define SPK_I2S_GPIO_DATA_OUT  ((gpio_num_t)I2S_SD_OUT_PIN)   // 7
#define SPK_I2S_GPIO_BCLK      ((gpio_num_t)SPK_I2S_BCLK_PIN) // 15
#define SPK_I2S_GPIO_LRCK      ((gpio_num_t)SPK_I2S_LRCK_PIN) // 16

#define I2S_TX_PORT            SPK_I2S_PORT
#define I2S_TX_SAMPLE_RATE     AUDIO_OUTPUT_SAMPLE_RATE
#define I2S_TX_BCLK_GPIO       SPK_I2S_GPIO_BCLK
#define I2S_TX_LRCK_GPIO       SPK_I2S_GPIO_LRCK
#define I2S_TX_DOUT_GPIO       SPK_I2S_GPIO_DATA_OUT

#define GPS_UART_NUM           UART_NUM_2
#define GPS_RX_GPIO            ((gpio_num_t)GPS_RX_PIN)
#define GPS_TX_GPIO            ((gpio_num_t)GPS_TX_PIN)
#define GPS_BAUDRATE           GPS_BAUD

#define LED_CRY_RED_GPIO       ((gpio_num_t)LED_CRY_RED_PIN)
#define LED_CRY_GREEN_GPIO     ((gpio_num_t)LED_CRY_GREEN_PIN)
#define LED_NIGHT_GPIO         ((gpio_num_t)LED_NIGHT_PIN)
#define MODE_BUTTON_GPIO       ((gpio_num_t)MODE_BUTTON_PIN)

#define I2C_SDA_GPIO           ((gpio_num_t)I2C_SDA_PIN)
#define I2C_SCL_GPIO           ((gpio_num_t)I2C_SCL_PIN)
#define I2C_FREQ_HZ            400000

#define NVS_NAMESPACE_AUDIO      "audio_cfg"
#define NVS_KEY_OUTPUT_VOLUME    "vol_out"

#endif // _BOARD_CONFIG_H_

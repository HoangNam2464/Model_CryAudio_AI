#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>

/* =========================
 *  FEATURE SWITCHES
 * ========================= */
#define USE_INMP441_MIC        1  // enable INMP441 microphone (I2S RX)
#define USE_MAX98357A_SPK      1  // enable MAX98357A amplifier (I2S TX)
#define USE_GPS_NEO6M          1  // GPS NEO-6M over UART2
#define USE_OLED_I2C           0  // reserved for future OLED

/* =========================
 *  AUDIO & AI PARAMETERS
 * ========================= */
#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT
#define INFER_INTERVAL_S             0.50f     // run inference every 0.5 s
#define I2S_READ_SAMPLES             1024      // samples pulled per i2s_read call

/* =========================
 *  MIC INMP441 (I2S RX) - I2S0
 * ========================= */
#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        GPIO_NUM_25
#define MIC_I2S_GPIO_BCLK      GPIO_NUM_26
#define MIC_I2S_GPIO_DATA_IN   GPIO_NUM_34   // input-only

/* =========================
 *  SPEAKER MAX98357A (I2S TX) - I2S1
 * ========================= */
#define SPK_I2S_PORT           I2S_NUM_1
#define SPK_I2S_GPIO_BCLK      GPIO_NUM_27
#define SPK_I2S_GPIO_LRCLK     GPIO_NUM_14
#define SPK_I2S_GPIO_DATA_OUT  GPIO_NUM_23

/* =========================
 *  GPS NEO-6M (UART2)
 * ========================= */
#define GPS_UART_NUM           UART_NUM_2
#define GPS_RX_PIN             GPIO_NUM_16
#define GPS_TX_PIN             GPIO_NUM_17
#define GPS_BAUDRATE           9600

/* =========================
 *  LED / BUTTON / POWER
 * ========================= */
#define BOOT_BUTTON_GPIO       GPIO_NUM_0
#define BUILTIN_LED_GPIO       GPIO_NUM_2
#define ALARM_LED_GPIO         GPIO_NUM_2

/* =========================
 *  I2C BUS (OLED, SENSOR...)
 * ========================= */
#define I2C_SDA_PIN            GPIO_NUM_21
#define I2C_SCL_PIN            GPIO_NUM_22
#define I2C_FREQ_HZ            400000

/* =========================
 *  NETWORK & BACKEND NOTES
 * =========================
 *  Keep WiFi credentials and backend tokens in include/Config.h
 *  so there is a single source of truth.
 * ========================= */

#endif // _BOARD_CONFIG_H_

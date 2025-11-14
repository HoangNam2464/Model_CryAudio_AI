#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s.h>

/* ============================================================
 *  FEATURE SWITCHES – Bật/Tắt module đúng theo phần cứng thực tế
 * ============================================================ */
#define USE_INMP441_MIC        1   // Mic INMP441 I2S RX
#define USE_MAX98357A_SPK      1   // Amp MAX98357A I2S TX
#define USE_GPS_NEO6M          1   // GPS NEO-6M UART2
#define USE_OLED_I2C           0   // Không dùng OLED (dự án hiện tại)

/* ============================================================
 *  AUDIO / AI PARAMETERS – cấu hình âm thanh và AI
 * ============================================================ */
#define AUDIO_INPUT_SAMPLE_RATE      16000
#define AUDIO_OUTPUT_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE_RX     I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_BITS_PER_SAMPLE_TX     I2S_BITS_PER_SAMPLE_16BIT

#define I2S_READ_SAMPLES             1024      // Block đọc mic mỗi lần
#define INFER_INTERVAL_S             2.0f      // Chu kỳ chạy AI (2 giây/lần)

/* ============================================================
 *  I2S MICROPHONE – INMP441 (Dùng I2S0 – ESP32 DevKit 30 chân)
 *
 *  Dây phần cứng thực tế:
 *     WS/LRCL  → GPIO25  (D25)
 *     BCLK     → GPIO26  (D26)
 *     DATA OUT → GPIO22  (D22)    <-- mic SD
 * ============================================================ */
#define MIC_I2S_PORT           I2S_NUM_0
#define MIC_I2S_GPIO_WS        GPIO_NUM_25
#define MIC_I2S_GPIO_BCLK      GPIO_NUM_26
#define MIC_I2S_GPIO_DATA_IN   GPIO_NUM_22

/* ============================================================
 *  I2S SPEAKER – MAX98357A (TX trên cùng I2S0)
 *
 *  Dây phần cứng thực tế:
 *     BCLK  → GPIO26 (chung clock mic)
 *     LRC   → GPIO25 (chung WS mic)
 *     DIN   → GPIO27 (D27)  <-- data out
 *     SD    → 3V3 hoặc GND
 * ============================================================ */
#define SPK_I2S_PORT           I2S_NUM_0      // full-duplex
#define SPK_I2S_GPIO_BCLK      GPIO_NUM_26
#define SPK_I2S_GPIO_LRCLK     GPIO_NUM_25
#define SPK_I2S_GPIO_DATA_OUT  GPIO_NUM_27

/* ============================================================
 *  GPS NEO-6M – UART2 (Dùng RX2/TX2)
 *
 *  Dây phần cứng thực tế:
 *     GPS TX → GPIO16 (RX2)
 *     GPS RX → GPIO17 (TX2)
 * ============================================================ */
#define GPS_UART_NUM           UART_NUM_2
#define GPS_RX_PIN             GPIO_NUM_16
#define GPS_TX_PIN             GPIO_NUM_17
#define GPS_BAUDRATE           9600

/* ============================================================
 *  LED / BUTTON
 * ============================================================ */
#define BUILTIN_LED_GPIO       GPIO_NUM_2     // LED on-board
#define ALARM_LED_GPIO         GPIO_NUM_2     // Dùng chung cho cảnh báo
#define BOOT_BUTTON_GPIO       GPIO_NUM_0     // Nút BOOT

/* ============================================================
 *  I2C BUS – tắt vì không dùng OLED
 * ============================================================ */
#define I2C_SDA_PIN            GPIO_NUM_21
#define I2C_SCL_PIN            GPIO_NUM_22
#define I2C_FREQ_HZ            400000         // 400kHz

/* ============================================================
 *  NETWORK / BACKEND
 *
 *  Ghi chú:
 *   - Token/API được đặt trong file include/Config.h
 *   - WiFi SSID/PASS cấu hình qua portal (/wifi) nếu bật captive portal
 * ============================================================ */

#endif // _BOARD_CONFIG_H_

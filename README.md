# AudioCryESP32

ESP32 (ESP32-D0WD-V3) baby-cry detector with I2S microphone, TinyGPS++, on-device TFLite Micro inference, HTTP uploader, and a small local status API.

## Hardware

- Mic I2S **INMP441** (BCLK=GPIO26, WS=GPIO25, DOUT=GPIO34, VDD=3V3, GND)
- GPS **NEO-6M** (Serial1 RX=GPIO16, TX=GPIO17, VCC=3V3, GND)
- LED status on GPIO2 (can reuse alarm LED)
- MAX98357A class-D amp (BCLK=GPIO27, LRCK=GPIO14, DIN=GPIO23, SD optional)

## Pin map (`include/Config.h`)

All runtime pins, WiFi credentials, backend URLs, and GPS fallbacks live in `include/Config.h`. Update that file when you rewire or move devices.

| Device            | Signals             | GPIO (default) | Notes                    |
| ----------------- | ------------------- | -------------- | ------------------------ |
| LED status        | LED                 | GPIO2          | Built-in LED supported   |
| Mic INMP441       | BCLK / WS / DOUT    | 26 / 25 / 34   | Matches `I2S_*` macros   |
| GPS NEO-6M        | RX / TX             | 16 / 17        | Serial1/UART2            |
| MAX98357A         | BCLK / LRCK / DIN   | 27 / 14 / 23   | Enable `USE_MAX98357A*`  |
| GPS fallback LL   | Lat / Lng           | Config.h       | Used when fix invalid    |

Refer to `include/board_config.h` for a terse summary of board-level wiring; no credentials are stored there anymore.

## Quick start

1. Export your INT8 TFLite model to `src/crynet_model.cc` (for example with `convert/tflite_to_cc.py --var_name crynet_int8_tflite ...`). Keep the declarations in `include/crynet_model.h` in sync.
2. Ensure `lib/tflite-micro-minimal/` (or your preferred TFLM build) is available under `lib/`.
3. Update WiFi SSID/password, backend URL/token, and any GPS defaults inside `include/Config.h` (this is the single source of truth).
4. Build & upload with PlatformIO (`platformio.ini` already pulls TinyGPS++ and ArduinoJson).

## Runtime notes

- Audio capture runs at 16 kHz mono; `taskMic` pushes 1024-sample chunks into a FreeRTOS queue.
- `lib/TflmInfer/tflm_infer.cpp` now performs full log-mel feature extraction plus TFLM inference. Increase `kTensorArenaSize` there if `AllocateTensors` fails.
- Cry/no-cry debounce is handled by `lib/CryDetector`, configurable via constructor parameters in `src/main.cpp`.
- Local HTTP server exposes `/` (health) and `/status` (JSON payload `{ device_id, ip, prob, score, crying, lat, lng, gps_valid }`).

#pragma once

#include <cstddef>
#include <cstdint>
#include "audio_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

enum class AudioJobType : uint8_t
{
    PLAY_TONE_TEST,
    PLAY_PCM_BLOCK,
};

struct AudioJob
{
    AudioJobType type;
    const int16_t *pcm;
    size_t samples;
};

class AudioService
{
public:
    explicit AudioService(AudioCodec *codec);

    esp_err_t Start();

    // Test loa: 440 Hz, 2s on / 1s off
    void StartTestToneTask();
    void StopTestToneTask();

    // Gửi block PCM 16 kHz ra loa (service có thể resample nếu cần)
    bool EnqueuePcm(const int16_t *pcm, size_t samples, TickType_t timeout);

private:
    static void input_task_trampoline(void *arg);
    static void output_task_trampoline(void *arg);
    static void test_tone_task_trampoline(void *arg);

    void input_task();
    void output_task();
    void test_tone_task();

    AudioCodec *_codec = nullptr;

    TaskHandle_t _input_task_handle = nullptr;
    TaskHandle_t _output_task_handle = nullptr;
    TaskHandle_t _tone_task_handle = nullptr;

    QueueHandle_t _output_queue = nullptr;

    bool _running = false;
};

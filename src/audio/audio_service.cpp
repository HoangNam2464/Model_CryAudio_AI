#include "audio_service.h"
#include "esp_log.h"
#include "board_config.h"
#include <cmath>
#include <cstring>

static const char *TAG_AS = "AudioService";

AudioService::AudioService(AudioCodec *codec)
    : _codec(codec)
{
}

esp_err_t AudioService::Start()
{
    if (!_codec)
        return ESP_FAIL;

    _output_queue = xQueueCreate(4, sizeof(AudioJob));
    if (!_output_queue)
    {
        ESP_LOGE(TAG_AS, "Failed to create output queue");
        return ESP_FAIL;
    }

    _codec->EnableInput(true);
    _codec->EnableOutput(true);

    xTaskCreatePinnedToCore(input_task_trampoline, "audio_input",
                            4096, this, 5, &_input_task_handle, 1);
    xTaskCreatePinnedToCore(output_task_trampoline, "audio_output",
                            4096, this, 5, &_output_task_handle, 1);

    _running = true;
    ESP_LOGI(TAG_AS, "AudioService started");
    return ESP_OK;
}

void AudioService::input_task_trampoline(void *arg)
{
    static_cast<AudioService *>(arg)->input_task();
}

void AudioService::output_task_trampoline(void *arg)
{
    static_cast<AudioService *>(arg)->output_task();
}

void AudioService::test_tone_task_trampoline(void *arg)
{
    static_cast<AudioService *>(arg)->test_tone_task();
}

void AudioService::input_task()
{
    // Skeleton: đọc mic và chuyển cho AI (tùy bạn nối queue/buffer riêng)
    constexpr size_t BUF_SAMPLES = 512;
    int16_t buf[BUF_SAMPLES];

    while (true)
    {
        size_t n = _codec->InputData(buf, BUF_SAMPLES, 50);
        if (n > 0)
        {
            // TODO: đẩy vào queue/buffer AI
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void AudioService::output_task()
{
    AudioJob job;
    while (true)
    {
        if (xQueueReceive(_output_queue, &job, portMAX_DELAY) == pdTRUE)
        {
            if (job.type == AudioJobType::PLAY_PCM_BLOCK && job.pcm && job.samples > 0)
            {
                // Nếu cần, resample 16->24 kHz trước khi OutputData
                _codec->OutputData(job.pcm, job.samples, 100);
            }
        }
    }
}

bool AudioService::EnqueuePcm(const int16_t *pcm, size_t samples, TickType_t timeout)
{
    AudioJob job{
        .type = AudioJobType::PLAY_PCM_BLOCK,
        .pcm = pcm,
        .samples = samples};
    return xQueueSend(_output_queue, &job, timeout) == pdTRUE;
}

void AudioService::StartTestToneTask()
{
    if (_tone_task_handle)
        return;
    xTaskCreatePinnedToCore(test_tone_task_trampoline, "tone_test",
                            4096, this, 4, &_tone_task_handle, 1);
    ESP_LOGI(TAG_AS, "Test tone task started");
}

void AudioService::StopTestToneTask()
{
    if (_tone_task_handle)
    {
        vTaskDelete(_tone_task_handle);
        _tone_task_handle = nullptr;
        ESP_LOGI(TAG_AS, "Test tone task stopped");
    }
}

void AudioService::test_tone_task()
{
    const float freq = 440.0f;
    const int fs = I2S_TX_SAMPLE_RATE; // 24 kHz
    constexpr size_t BUF_SAMPLES = 512;
    int16_t buf[BUF_SAMPLES];

    while (true)
    {
        // 2s ON
        int total_on = fs * 2;
        int generated = 0;
        while (generated < total_on)
        {
            for (size_t i = 0; i < BUF_SAMPLES; ++i)
            {
                float t = static_cast<float>(generated + i) / fs;
                float s = sinf(2.0f * 3.14159265f * freq * t);
                buf[i] = static_cast<int16_t>(s * 30000);
            }
            _codec->OutputData(buf, BUF_SAMPLES, 100);
            generated += BUF_SAMPLES;
        }

        // 1s OFF
        int total_off = fs * 1;
        generated = 0;
        memset(buf, 0, sizeof(buf));
        while (generated < total_off)
        {
            _codec->OutputData(buf, BUF_SAMPLES, 100);
            generated += BUF_SAMPLES;
        }
    }
}

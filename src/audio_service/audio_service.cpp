#include "audio_service.h"
#include <FS.h>
#include <SPIFFS.h>
#include <driver/i2s.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <HardwareSerial.h>
#include "Config.h"
#include "board_config.h"

extern HardwareSerial Serial0;

// ===================================================
//  SPIFFS INIT
// ===================================================
namespace
{
    bool ensureFS()
    {
        static bool mounted = false;
        if (mounted)
            return true;

        mounted = SPIFFS.begin(true);
        if (!mounted)
        {
            Serial0.println("[AUDIO] SPIFFS mount failed");
        }
        else
        {
            Serial0.printf("[AUDIO] SPIFFS mounted, total=%u free=%u\n",
                           (unsigned)SPIFFS.totalBytes(),
                           (unsigned)(SPIFFS.totalBytes() - SPIFFS.usedBytes()));
        }
        return mounted;
    }

    // ===================================================
    //  PLAY PCM WAV — 16-BIT — MONO/STEREO
    // ===================================================
    bool playPcmWav(const char *path)
    {
        if (!path)
            return false;
        if (!ensureFS())
            return false;

        File f = SPIFFS.open(path, "r");
        if (!f)
        {
            Serial0.printf("[AUDIO] File not found: %s\n", path);
            return false;
        }

        // ---- read WAV header ----
        uint8_t header[44];
        if (f.read(header, sizeof(header)) != sizeof(header))
        {
            Serial0.printf("[AUDIO] WAV header too short: %s\n", path);
            f.close();
            return false;
        }

        if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        {
            Serial0.printf("[AUDIO] Invalid WAV (RIFF/WAVE): %s\n", path);
            f.close();
            return false;
        }

        uint16_t audioFormat = header[20] | (header[21] << 8);
        uint16_t numChannels = header[22] | (header[23] << 8);
        uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);

        if (audioFormat != 1)
        { // PCM only
            Serial0.printf("[AUDIO] Not PCM WAV: %s\n", path);
            f.close();
            return false;
        }

        if (sampleRate != I2S_SAMPLE_RATE)
        {
            Serial0.printf("[AUDIO] WARNING: WAV sampleRate=%u differs from I2S=%u → speed mismatch\n",
                           sampleRate, I2S_SAMPLE_RATE);
        }

        // ---- streaming buffer ----
        const size_t chunk = 1024;
        static std::vector<uint8_t> buf;
        static std::vector<int16_t> mono;

        if (buf.size() < chunk)
            buf.resize(chunk);

        Serial0.printf("[AUDIO] Playing PCM: %s (ch=%u rate=%u)\n", path, numChannels, sampleRate);

        while (true)
        {
            size_t r = f.read(buf.data(), chunk);
            if (r == 0)
                break;

            if (numChannels == 1)
            {
                // mono PCM
                size_t written = 0;
                i2s_write(SPK_I2S_PORT, buf.data(), r, &written, pdMS_TO_TICKS(200));
                if (written == 0)
                    break;
            }
            else
            {
                // stereo → convert to mono
                size_t samples = r / 4; // stereo 16-bit
                mono.resize(samples);
                const int16_t *src = reinterpret_cast<int16_t *>(buf.data());
                for (size_t i = 0; i < samples; i++)
                {
                    mono[i] = src[i * 2]; // lấy kênh trái
                }
                size_t written = 0;
                i2s_write(SPK_I2S_PORT, mono.data(), samples * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
                if (written == 0)
                    break;
            }
        }

        f.close();
        return true;
    }

    // ===================================================
    //  FALLBACK BEEP TONE
    // ===================================================
    void beepFallback(int freq = 1000, int durationMs = 200, int volume = 20000)
    {
        const size_t N = static_cast<size_t>(I2S_SAMPLE_RATE * (durationMs / 1000.0f));

        static std::vector<int16_t> buf;
        buf.resize(N);

        for (size_t i = 0; i < N; i++)
        {
            float env = sinf(3.14159f * i / N);
            float s = sinf(2.0f * 3.14159f * freq * i / I2S_SAMPLE_RATE) * env;
            buf[i] = static_cast<int16_t>(s * volume);
        }

        const uint8_t *ptr = reinterpret_cast<const uint8_t *>(buf.data());
        size_t remain = buf.size() * sizeof(int16_t);

        while (remain > 0)
        {
            size_t written = 0;
            i2s_write(SPK_I2S_PORT, ptr, remain, &written, pdMS_TO_TICKS(200));
            if (written == 0)
                break;
            ptr += written;
            remain -= written;
        }
    }

    // ===================================================
    // PLAY WITH FALLBACK — PCM ONLY
    // ===================================================
    void playFileOrBeep(const char *pcm)
    {
        if (!pcm)
        {
            beepFallback();
            return;
        }

        if (playPcmWav(pcm))
            return;

        Serial0.printf("[AUDIO] PCM failed: %s → fallback beep\n", pcm);
        beepFallback();
    }

} // namespace

// ===================================================
// PUBLIC API — gọi từ main
// ===================================================
bool audioInitFS()
{
    return ensureFS();
}

void playTestTone()
{
    Serial0.println("[AUDIO] Test tone 1kHz");
    beepFallback(1000, 2000, 26000);
}

void playCryAlert() { playFileOrBeep("/audio/cry.wav"); }
void playCalmAlert() { playFileOrBeep("/audio/calm.wav"); }
void playNightModeOn() { playFileOrBeep("/audio/night_on.wav"); }
void playNightModeOff() { playFileOrBeep("/audio/night_off.wav"); }
void playWifiSuccess() { playFileOrBeep("/audio/wifi_ok.wav"); }

// void playStartupSound()   { playFileOrBeep("/audio/startup.wav"); }
// void playErrorSound()     { playFileOrBeep("/audio/error.wav"); }

#include "audio_service.h"
#include <FS.h>
#include <SPIFFS.h>
#include <driver/i2s.h>
#include <vector>
#include <cstring>
#include "Config.h"
#include "board_config.h"
#include "ima_adpcm_decoder.h"

namespace {
bool ensureFS() {
    static bool mounted = false;
    if (mounted) return true;
    mounted = SPIFFS.begin(true);
    return mounted;
}

bool playAdpcmFile(const char* path) {
    if (!path) return false;
    if (!ensureFS()) return false;
    File f = SPIFFS.open(path, "r");
    if (!f) return false;
    WAVInfo w = parseWavIMA(f);
    if (!w.ok) { f.close(); return false; }
    if (w.sampleRate != I2S_SAMPLE_RATE) {
        // Không đổi clock để tránh ảnh hưởng MIC; giả định file 16 kHz.
        // Nếu khác, âm sẽ bị nhanh/chậm: cần convert lại.
    }
    const size_t blk = w.blockAlign ? w.blockAlign : 256;
    std::vector<uint8_t> adpcm(blk);
    std::vector<int16_t> pcm;
    pcm.resize((blk - 4) * 2 + 1);
    IMAState st{0, 0};
    f.seek(w.dataOffset);
    while (f.available()) {
        size_t r = f.read(adpcm.data(), blk);
        if (r < 4) break;
        size_t pcm_samples = 0;
        if (!decodeIMA4BitBlock(adpcm.data(), r, pcm.data(), pcm_samples, st)) break;
        size_t written = 0;
        i2s_write(SPK_I2S_PORT, pcm.data(), pcm_samples * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
        if (written == 0) break;
    }
    f.close();
    return true;
}

// Stream WAV (16-bit PCM, mono/stereo) từ SPIFFS; fallback khi thiếu ADPCM
bool playPcmWav(const char* path) {
    if (!path) return false;
    if (!ensureFS()) return false;
    File f = SPIFFS.open(path, "r");
    if (!f) return false;

    uint8_t header[44];
    if (f.read(header, sizeof(header)) != sizeof(header)) { f.close(); return false; }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) { f.close(); return false; }
    uint16_t audioFormat = header[20] | (header[21] << 8);
    uint16_t numChannels = header[22] | (header[23] << 8);
    if (audioFormat != 1) { f.close(); return false; } // only PCM

    const size_t chunk = 1024;
    static std::vector<uint8_t> buf;
    static std::vector<int16_t> mono;
    while (true) {
        size_t r = f.read(buf.data(), chunk);
        if (r == 0) break;
        if (numChannels == 1) {
            size_t written = 0;
            i2s_write(SPK_I2S_PORT, buf.data(), r, &written, pdMS_TO_TICKS(200));
            if (written == 0) break;
        } else {
            size_t samples = r / 4;  // stereo 16-bit
            mono.resize(samples);
            const int16_t* src = reinterpret_cast<const int16_t*>(buf.data());
            for (size_t i = 0; i < samples; ++i) {
                mono[i] = src[i * 2];  // left channel
            }
            size_t written = 0;
            i2s_write(SPK_I2S_PORT, mono.data(), samples * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
            if (written == 0) break;
        }
    }
    f.close();
    return true;
}
}  // namespace

bool audioInitFS() { return ensureFS(); }
static void playWithFallback(const char* adpcm_path, const char* pcm_path) {
    if (adpcm_path && playAdpcmFile(adpcm_path)) return;
    if (pcm_path) playPcmWav(pcm_path);
}

void playCryAlert()       { playWithFallback(AUDIO_ADPCM_CRY_ALERT, AUDIO_PCM_CRY_ALERT); }
void playCalmAlert()      { playWithFallback(AUDIO_ADPCM_CALM_ALERT, AUDIO_PCM_CALM_ALERT); }
void playNightModeOn()    { playWithFallback(AUDIO_ADPCM_NIGHT_ON, AUDIO_PCM_NIGHT_ON); }
void playNightModeOff()   { playWithFallback(AUDIO_ADPCM_NIGHT_OFF, AUDIO_PCM_NIGHT_OFF); }
void playStartupSound()   { playWithFallback(AUDIO_ADPCM_STARTUP, AUDIO_PCM_STARTUP); }
void playErrorSound()     { playWithFallback(AUDIO_ADPCM_ERROR, AUDIO_PCM_ERROR); }
void playWifiSuccess()    { playWithFallback(AUDIO_ADPCM_WIFI_SUCCESS, AUDIO_PCM_WIFI_SUCCESS); }

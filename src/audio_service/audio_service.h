#pragma once
#include <Arduino.h>

// Ưu tiên file ADPCM 4-bit (IMA) để tiết kiệm dung lượng. Nếu không tồn tại sẽ fallback sang PCM.
constexpr const char* AUDIO_ADPCM_CRY_ALERT     = "/audio/cry_adpcm.wav";
constexpr const char* AUDIO_ADPCM_CALM_ALERT    = "/audio/calm_adpcm.wav";
constexpr const char* AUDIO_ADPCM_NIGHT_ON      = "/audio/night_on_adpcm.wav";
constexpr const char* AUDIO_ADPCM_NIGHT_OFF     = "/audio/night_off_adpcm.wav";
constexpr const char* AUDIO_ADPCM_STARTUP       = nullptr;
constexpr const char* AUDIO_ADPCM_ERROR         = nullptr;
constexpr const char* AUDIO_ADPCM_WIFI_SUCCESS  = "/audio/wifi_ok_adpcm.wav";

// PCM fallback
constexpr const char* AUDIO_PCM_CRY_ALERT      = "/audio/cry.wav";
constexpr const char* AUDIO_PCM_CALM_ALERT     = "/audio/calm.wav";
constexpr const char* AUDIO_PCM_NIGHT_ON       = "/audio/night_on.wav";
constexpr const char* AUDIO_PCM_NIGHT_OFF      = "/audio/night_off.wav";
constexpr const char* AUDIO_PCM_STARTUP        = nullptr;
constexpr const char* AUDIO_PCM_ERROR          = nullptr;
constexpr const char* AUDIO_PCM_WIFI_SUCCESS   = "/audio/wifi_ok.wav";

bool audioInitFS();
void playCryAlert();
void playCalmAlert();
void playNightModeOn();
void playNightModeOff();
void playStartupSound();
void playErrorSound();
void playWifiSuccess();
// Tone test: phát beep 1 kHz ~2s để kiểm tra phần cứng loa/I2S
void playTestTone();

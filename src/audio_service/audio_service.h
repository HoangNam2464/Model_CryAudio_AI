#pragma once
#include <Arduino.h>

// Ưu tiên file ADPCM 4-bit (IMA) để tiết kiệm dung lượng. Nếu không tồn tại sẽ fallback sang PCM.
constexpr const char* AUDIO_ADPCM_CRY_ALERT     = "/audio/Em_be_dang_khoc_adpcm.wav";
constexpr const char* AUDIO_ADPCM_CALM_ALERT    = "/audio/Em_be_da_ngu_yen_adpcm.wav";
constexpr const char* AUDIO_ADPCM_NIGHT_ON      = "/audio/Da_chuyen_sang_che_do_ban_dem_adpcm.wav";
constexpr const char* AUDIO_ADPCM_NIGHT_OFF     = "/audio/Da_chuyen_sang_che_do_ban_ngay_adpcm.wav";
constexpr const char* AUDIO_ADPCM_STARTUP       = nullptr;
constexpr const char* AUDIO_ADPCM_ERROR         = nullptr;
constexpr const char* AUDIO_ADPCM_WIFI_SUCCESS  = "/audio/Da_ket_noi_mang_thanh_cong_Vui_long_kiem_tra_lai_adpcm.wav";

// PCM fallback
constexpr const char* AUDIO_PCM_CRY_ALERT      = "/audio/Em_be_dang_khoc.wav";
constexpr const char* AUDIO_PCM_CALM_ALERT     = "/audio/Em_be_da_ngu_yen.wav";
constexpr const char* AUDIO_PCM_NIGHT_ON       = "/audio/Da_chuyen_sang_che_do_ban_dem.wav";
constexpr const char* AUDIO_PCM_NIGHT_OFF      = "/audio/Da_chuyen_sang_che_do_ban_ngay.wav";
constexpr const char* AUDIO_PCM_STARTUP        = nullptr;
constexpr const char* AUDIO_PCM_ERROR          = nullptr;
constexpr const char* AUDIO_PCM_WIFI_SUCCESS   = "/audio/Da_ket_noi_mang_thanh_cong_Vui_long_kiem_tra_lai.wav";

bool audioInitFS();
void playCryAlert();
void playCalmAlert();
void playNightModeOn();
void playNightModeOff();
void playStartupSound();
void playErrorSound();
void playWifiSuccess();

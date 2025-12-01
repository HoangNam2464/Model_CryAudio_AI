#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// Callback báo Wi-Fi đã kết nối
extern void onWifiConnected();

// Initialize Wi-Fi service (event group, prefs).
void wifi_service_init();

// Start Wi-Fi task that manages STA/AP and reconnection loop.
void wifi_service_start();

// Wait until Wi-Fi is ready (connected) within waitMs.
bool wifi_ensure_connected(uint32_t waitMs = 5000);

// Request a reconnect attempt and reset backoff.
void wifi_request_reconnect();

// Clear the "no credentials" pause so STA attempts resume immediately.
void wifi_clear_no_cred_pause();

// Setup AP state helpers.
bool wifi_is_setup_ap_active();
const char *wifi_get_setup_ap_ssid();
const char *wifi_get_setup_ap_pass();

// LED helpers.
void wifi_update_led();
void wifi_blink_led(uint8_t times);

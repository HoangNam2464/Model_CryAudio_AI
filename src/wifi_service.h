#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// Khởi tạo service Wi-Fi (event group, AP cấu hình)
void wifi_service_init();

// Tạo task Wi-Fi xử lý kết nối/reconnect
void wifi_service_start();

// Chờ Wi-Fi sẵn sàng trong khoảng waitMs, trả về true nếu đã sẵn sàng
bool wifi_ensure_connected(uint32_t waitMs = 5000);

// Yêu cầu reconnect và mở lại AP cấu hình
void wifi_request_reconnect();

// Trạng thái SoftAP cấu hình
bool wifi_is_setup_ap_active();
const char *wifi_get_setup_ap_ssid();
const char *wifi_get_setup_ap_pass();

// LED Wi-Fi
void wifi_update_led();
void wifi_blink_led(uint8_t times);

#include "Config.h"
#include "gps.h"

#include <TinyGPSPlus.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {

static HardwareSerial GPS_Serial(2);
static TinyGPSPlus gps;
static GpsFix lastFix = {0.0, 0.0, false, 0};
static uint32_t lastFixMs = 0;
static portMUX_TYPE fixLock = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t kFixTimeoutMs = 5000;

void UpdateFixUnlocked(TinyGPSLocation& loc) {
    lastFix.lat = loc.lat();
    lastFix.lng = loc.lng();
    lastFix.valid = loc.isValid();
    lastFix.age_ms = loc.age();
    lastFixMs = millis();
}

}  // namespace

void gps_begin() {
    GPS_Serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void gps_loop() {
    while (GPS_Serial.available()) {
        gps.encode(static_cast<char>(GPS_Serial.read()));
    }

    const uint32_t now = millis();
    if (gps.location.isUpdated()) {
        portENTER_CRITICAL(&fixLock);
        UpdateFixUnlocked(gps.location);
        portEXIT_CRITICAL(&fixLock);
    } else {
        portENTER_CRITICAL(&fixLock);
        if (lastFix.valid && (now - lastFixMs) > kFixTimeoutMs) {
            lastFix.valid = false;
            lastFix.age_ms = now - lastFixMs;
        }
        portEXIT_CRITICAL(&fixLock);
    }
}

GpsFix gps_get_fix() {
    GpsFix copy;
    portENTER_CRITICAL(&fixLock);
    copy = lastFix;
    portEXIT_CRITICAL(&fixLock);
    return copy;
}

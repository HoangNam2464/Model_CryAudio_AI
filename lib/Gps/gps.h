#pragma once
#include <Arduino.h>
struct GpsFix { double lat; double lng; bool valid; uint32_t age_ms; };
void gps_begin();
void gps_loop();
GpsFix gps_get_fix();

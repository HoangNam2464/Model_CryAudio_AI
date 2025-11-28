#pragma once

#include <Arduino.h>

struct WifiCredentials
{
    String ssid;
    String pass;
};

// Load credentials from NVS; falls back to WIFI_SSID/WIFI_PASS only if nothing is stored.
void wifi_config_init();
void wifi_config_load(WifiCredentials &creds);
void wifi_config_save(const WifiCredentials &creds);
bool wifi_config_has_credentials();
void wifi_config_clear();

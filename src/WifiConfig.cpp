#include "WifiConfig.h"

#include <Preferences.h>

#include "Config.h"

static Preferences prefs;
static bool prefsOpened = false;

static void ensurePrefs(){
    if (!prefsOpened){
        prefs.begin("wifi_cfg", false);
        prefsOpened = true;
    }
}

void wifi_config_init(){
    ensurePrefs();
}

void wifi_config_load(WifiCredentials& creds){
    ensurePrefs();
    creds.ssid = prefs.getString("ssid", WIFI_SSID);
    creds.pass = prefs.getString("pass", WIFI_PASS);
}

void wifi_config_save(const WifiCredentials& creds){
    ensurePrefs();
    prefs.putString("ssid", creds.ssid);
    prefs.putString("pass", creds.pass);
}

bool wifi_config_has_credentials(){
    ensurePrefs();
    return prefs.isKey("ssid");
}

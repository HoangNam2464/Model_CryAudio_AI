#include "WifiConfig.h"

#include <Preferences.h>

#include "Config.h"

static Preferences prefs;
static bool prefsOpened = false;

static void ensurePrefs()
{
    if (!prefsOpened)
    {
        prefs.begin("wifi_cfg", false);
        prefsOpened = true;
    }
}

void wifi_config_init()
{
    ensurePrefs();
}

static bool nvs_has_nonempty_ssid()
{
    ensurePrefs();
    if (!prefs.isKey("ssid"))
        return false;
    String ssid = prefs.getString("ssid", "");
    return ssid.length() > 0;
}

bool wifi_config_has_credentials()
{
    return nvs_has_nonempty_ssid();
}

void wifi_config_load(WifiCredentials &creds)
{
    ensurePrefs();
    creds.ssid = prefs.getString("ssid", "");
    creds.pass = prefs.getString("pass", "");

    // Only fallback to compile-time defaults when nothing is stored.
    if (!nvs_has_nonempty_ssid() && strlen(WIFI_SSID) > 0)
    {
        creds.ssid = WIFI_SSID;
        creds.pass = WIFI_PASS;
    }
}

void wifi_config_save(const WifiCredentials &creds)
{
    ensurePrefs();
    prefs.putString("ssid", creds.ssid);
    prefs.putString("pass", creds.pass);
}

void wifi_config_clear()
{
    ensurePrefs();
    if (prefs.isKey("ssid"))
        prefs.remove("ssid");
    if (prefs.isKey("pass"))
        prefs.remove("pass");
}

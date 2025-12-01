#include "utils_time.h"
#include <WiFi.h>

void initNtp()
{
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("[NTP] Waiting sync...");
    delay(2000);
}

String currentTimestamp()
{
    time_t now;
    time(&now);

    if (now < 100000)
        return "1970-01-01 00:00:00";

    struct tm *timeinfo;
    timeinfo = localtime(&now);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buf);
}

#include "device_id.h"
#include "Config.h"
#include <WiFi.h>

String device_id_str()
{
    // Nếu người dùng đã cấu hình chuỗi ID cố định thì dùng luôn
    if (strlen(DEVICE_ID) > 0)
        return String(DEVICE_ID);

    // Mặc định: tạo từ MAC để tránh trùng lặp giữa các board
    uint64_t mac = ESP.getEfuseMac();
    char buf[20];
    snprintf(buf, sizeof(buf), "AC-%02X%02X%02X%02X%02X%02X",
             (unsigned)((mac >> 40) & 0xFF),
             (unsigned)((mac >> 32) & 0xFF),
             (unsigned)((mac >> 24) & 0xFF),
             (unsigned)((mac >> 16) & 0xFF),
             (unsigned)((mac >> 8) & 0xFF),
             (unsigned)(mac & 0xFF));
    return String(buf);
}

int device_id_int()
{
    // Nếu DEVICE_ID là số thì parse, nếu không thì lấy 3 byte cuối MAC
    if (strlen(DEVICE_ID) > 0)
    {
        char *end = nullptr;
        long v = strtol(DEVICE_ID, &end, 10);
        if (end && *end == '\0' && v > 0 && v <= 0x7FFFFFFF)
            return static_cast<int>(v);
    }
    uint64_t mac = ESP.getEfuseMac();
    return static_cast<int>(mac & 0xFFFFFF); // 3 byte cuối, tránh trùng
}

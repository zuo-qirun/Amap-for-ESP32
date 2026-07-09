#pragma once

// Wi-Fi: set these to the car hotspot or any shared AP used by phone + ESP32.
// They are fallback credentials. The web config portal can override them and
// stores new credentials in ESP32 NVS.
#define AMAP_WIFI_SSID "car_hotspot_ssid"
#define AMAP_WIFI_PASSWORD "car_hotspot_password"

// Config portal. When Wi-Fi is not configured or cannot connect, ESP32 starts
// an AP and captive portal at http://192.168.4.1/.
#define AMAP_CONFIG_AP_SSID_PREFIX "AMap-ESP32-"
// Empty or shorter than 8 chars means an open AP. Use 8+ chars to require a
// password.
#define AMAP_CONFIG_AP_PASSWORD ""
#define AMAP_CONFIG_AP_CHANNEL 6
#define AMAP_CONFIG_AP_MAX_CLIENTS 4
#define AMAP_WIFI_CONNECT_TIMEOUT_MS 15000UL
#define AMAP_WIFI_RETRY_MS 15000UL

// Firmware identity. CI overrides these build flags for dev/stable artifacts.
#ifndef AMAP_FIRMWARE_VERSION
#define AMAP_FIRMWARE_VERSION "0.1.0"
#endif

#ifndef AMAP_FIRMWARE_BUILD
#define AMAP_FIRMWARE_BUILD 1
#endif

#ifndef AMAP_FIRMWARE_CHANNEL
#define AMAP_FIRMWARE_CHANNEL "dev"
#endif

// OTA. ESP32 downloads manifests and firmware only from your domestic server.
// Example final URLs:
//   https://example.cn/ota/dev/manifest.json
//   https://example.cn/ota/stable/firmware.bin
#define OTA_BASE_URL "https://example.cn/ota"
#define OTA_CHANNEL AMAP_FIRMWARE_CHANNEL
#define OTA_CHECK_INTERVAL_MS 3600000UL
#define OTA_HTTP_TIMEOUT_MS 15000UL
#define OTA_FALLBACK_MAX_DEV_BOOT_ATTEMPTS 3
#define OTA_HEALTHY_MARK_DELAY_MS 8000UL

#define AMAP_UDP_PORT 4210
#define AMAP_PACKET_BUFFER_SIZE 1200
#define AMAP_STALE_MS 3000UL
#define AMAP_STANDBY_MS 10000UL

// ESP32-S3 I2C pins. Change to match your board wiring.
#define AMAP_OLED_SDA_PIN 8
#define AMAP_OLED_SCL_PIN 9
#define AMAP_OLED_RESET_PIN 255

// U8g2 expects the 8-bit OLED address. 0x78 equals the common 7-bit 0x3C.
#define AMAP_OLED_I2C_ADDRESS 0x78

#define AMAP_OLED_DRIVER_SSD1306_12864 1
#define AMAP_OLED_DRIVER_SH1106_12864 2
#define AMAP_OLED_DRIVER_SH1107_128128 3

// Default: common 0.96" SSD1306 128x64 I2C OLED.
#define AMAP_OLED_DRIVER AMAP_OLED_DRIVER_SSD1306_12864

// 1 enables a U8g2 CJK-capable font for Chinese roads/labels. Set to 0 if
// firmware size is tight or your display only needs ASCII.
#define AMAP_USE_CHINESE_FONT 1

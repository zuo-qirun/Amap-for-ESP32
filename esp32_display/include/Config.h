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
//   http://ota.zuoqirun.top/ota/dev/manifest.json
//   http://ota.zuoqirun.top/ota/stable/firmware.bin
#define OTA_BASE_URL "http://ota.zuoqirun.top/ota"
#define OTA_CHANNEL AMAP_FIRMWARE_CHANNEL
#define OTA_CHECK_INTERVAL_MS 3600000UL
#define OTA_HTTP_TIMEOUT_MS 15000UL
#define OTA_FALLBACK_MAX_DEV_BOOT_ATTEMPTS 3
#define OTA_HEALTHY_MARK_DELAY_MS 8000UL

#define AMAP_UDP_PORT 4210
// BLE uses a Nordic-UART-style service with a write-only RX characteristic.
// Android splits each JSON snapshot into framed chunks and the ESP32 parses it
// only after every chunk has arrived.
#define AMAP_BLE_DEVICE_NAME_PREFIX "AMap-ESP32-"
#define AMAP_BLE_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define AMAP_BLE_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define AMAP_BLE_MTU 517
// Rich route, road and service-area fields can exceed the old 1200-byte
// snapshot. The Android sender compacts verbose fields when needed.
#define AMAP_PACKET_BUFFER_SIZE 2048
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

// The OLED renderer is retained only as legacy source code. Hardware output is
// now always the selected 2.8-inch ST7789 module (240x320 physical pixels),
// pixels and is normally used in 320x240 landscape (rotation 1). Its adapter
// exposes VCC, GND, CS, RESET, DC, MOSI, SCK, LED and optional MISO; no touch
// pins are needed for the no-touch module. This S3 mapping avoids the USB
// pins, bootstrapping pins and the OLED's GPIO8/GPIO9 I2C bus.
#define AMAP_TFT_DRIVER_ST7789 1
#define AMAP_TFT_DRIVER_ILI9341 2
#ifndef AMAP_TFT_DRIVER
#define AMAP_TFT_DRIVER AMAP_TFT_DRIVER_ST7789
#endif
#define AMAP_TFT_WIDTH 320
#define AMAP_TFT_HEIGHT 240
#define AMAP_TFT_ROTATION 1
#define AMAP_TFT_SCLK_PIN 12
#define AMAP_TFT_MOSI_PIN 11
#define AMAP_TFT_MISO_PIN 13
#define AMAP_TFT_CS_PIN 10
#define AMAP_TFT_DC_PIN 14
#define AMAP_TFT_RST_PIN 15
#define AMAP_TFT_BL_PIN 16
// Adafruit's ST7789 init sequence enables panel inversion by default. This
// module displays the intended RGB565 colours with inversion disabled.
#define AMAP_TFT_INVERT_COLORS 0

// 1 enables a U8g2 CJK-capable font for Chinese roads/labels. Set to 0 if
// firmware size is tight or your display only needs ASCII.
#define AMAP_USE_CHINESE_FONT 1

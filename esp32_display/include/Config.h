#pragma once

// Wi-Fi: set these to the car hotspot or any shared AP used by phone + ESP32.
#define AMAP_WIFI_SSID "car_hotspot_ssid"
#define AMAP_WIFI_PASSWORD "car_hotspot_password"

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

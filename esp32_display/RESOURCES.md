# Navigation resources

The generated `NaviLinkIcons.cpp` asset set is derived from Navi-Link. See
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for upstream links,
the imported resource scope, and the upstream license status.

`NavigationIcons` stores original 1-bit XBM-style artwork in ESP32 program
memory. It is part of the application image, so no SPIFFS mount, PNG decoder,
or external SD card is required.

- Turn icons: straight, left/right, slight left/right, U-turn, keep left/right.
  They cover AMap `NEW_ICON` / `ICON` values 2--8 and 18--19.
- Camera icons: speed, monitoring, red-light, bus-lane, ETC, and warning.
  They cover the known `CAMERA_TYPE` groups and use monitoring as the safe
  fallback for unknown types.
- Lane glyphs: AMap drive-way codes 0--48 plus 62, 85, and 89 are represented
  as direction and enabled-direction bitmasks. This supports multi-direction
  lanes and the partial-enabled 30--48 family without storing 49 bitmap files.

The bitmap payload is about 400 bytes. The lane catalogue is a few hundred
bytes of code/data, substantially smaller than a full PNG resource pack. The
same API is used by the OLED now and is intended for the SPI TFT renderer.

The active PlatformIO target is ESP32-S3 N16R8. Its dedicated 16 MB dual-OTA
partition table leaves two 5 MB app slots and about 5.9 MB for SPIFFS assets.
The 8 MB OPI PSRAM can hold a 240x320 RGB565 frame buffer (153,600 bytes) when
the ST7789 renderer is enabled; a future no-PSRAM target can use the same UI
with line-buffer rendering instead.

## ST7789 wiring for ESP32-S3 N16R8

The no-touch 2.8-inch module is used in landscape. Keep the default OLED
profile until the new display is connected, then set
`AMAP_DISPLAY_PROFILE` to `AMAP_DISPLAY_PROFILE_TFT` in `include/Config.h`.

| Module pin | ESP32-S3 GPIO |
| --- | --- |
| SCK | 12 |
| SDI / MOSI | 11 |
| SDO / MISO (optional) | 13 |
| CS | 10 |
| RESET | 15 |
| DC / RS | 14 |
| LED | 16 |
| VCC / GND | 3.3 V / GND |

The ST7789 renderer uses 40 MHz hardware SPI, direct-mode drawing, Chinese
UTF-8 text, and the navigation icon/traffic/lane data already carried by the
phone protocol. No touch pins are used.

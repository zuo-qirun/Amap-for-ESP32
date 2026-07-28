#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Downloads one resolved album cover at a time on a background FreeRTOS task.
// Decoded RGB565 pixels live in PSRAM and are reused until the song changes.
class AlbumArtCache {
public:
  static constexpr int16_t SIZE = 128;
  static constexpr int16_t BACKDROP_WIDTH = 320;
  static constexpr int16_t BACKDROP_HEIGHT = 240;

  static AlbumArtCache& instance();
  void request(const String& coverUrl, bool networkConnected);
  bool draw(Adafruit_GFX& display, int16_t x, int16_t y, int16_t size = SIZE);
  String status();
  uint32_t revision() const { return generation; }
  // Draws a cached, genuinely low-pass-filtered cover backdrop. The expensive
  // blur is built once on the download task, never in the 25 FPS render loop.
  bool drawBlurred(Adafruit_GFX& display, int16_t x, int16_t y, int16_t width,
                   int16_t height, int16_t cellSize = 18,
                   uint8_t sourceOpacity = 255, uint16_t background = 0x0000);
  // Restore only a screen-space rectangle from the cached 320x240 backdrop.
  // Animation frames use this to avoid scanning unchanged background pixels.
  bool drawBlurredRegion(Adafruit_GFX& display, int16_t x, int16_t y,
                         int16_t width, int16_t height,
                         uint8_t sourceOpacity = 255,
                         uint16_t background = 0x0000);
  uint16_t dominantColor(uint16_t fallback);

private:
  struct DownloadRequest;

  AlbumArtCache() = default;
  bool ensureReady();
  bool downloadAndDecode(const String& coverUrl, String& error);
  static void downloadTask(void* parameter);

  SemaphoreHandle_t mutex = nullptr;
  uint16_t* frontBuffer = nullptr;
  uint16_t* backBuffer = nullptr;
  uint16_t* frontBlurBuffer = nullptr;
  uint16_t* backBlurBuffer = nullptr;
  uint16_t* styledBlurBuffer = nullptr;
  uint32_t styledGeneration = UINT32_MAX;
  uint8_t styledOpacity = 0;
  uint16_t styledBackground = 0;
  uint16_t frontPaletteColor = 0;
  uint16_t backPaletteColor = 0;
  String requestedUrl;
  String loadedUrl;
  String lastError;
  bool loading = false;
  unsigned long lastAttemptAt = 0;
  volatile uint32_t generation = 0;
};

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

  static AlbumArtCache& instance();
  void request(const String& coverUrl, bool networkConnected);
  bool draw(Adafruit_GFX& display, int16_t x, int16_t y, int16_t size = SIZE);
  String status();
  uint32_t revision() const { return generation; }
  // A deliberately low-detail enlarged cover makes a lightweight, album-led
  // backdrop without retaining another full-screen bitmap in PSRAM.
  bool drawBlurred(Adafruit_GFX& display, int16_t x, int16_t y, int16_t width,
                   int16_t height, int16_t cellSize = 18,
                   uint8_t sourceOpacity = 255, uint16_t background = 0x0000);
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
  String requestedUrl;
  String loadedUrl;
  String lastError;
  bool loading = false;
  unsigned long lastAttemptAt = 0;
  volatile uint32_t generation = 0;
};

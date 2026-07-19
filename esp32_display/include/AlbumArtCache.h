#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Downloads one NetEase album cover at a time on a background FreeRTOS task.
// Decoded RGB565 pixels live in PSRAM and are reused until the song changes.
class AlbumArtCache {
public:
  static constexpr int16_t SIZE = 128;

  static AlbumArtCache& instance();
  void request(const String& coverUrl, bool networkConnected);
  bool draw(Adafruit_GFX& display, int16_t x, int16_t y);

private:
  struct DownloadRequest;

  AlbumArtCache() = default;
  bool ensureReady();
  bool downloadAndDecode(const String& coverUrl);
  static void downloadTask(void* parameter);

  SemaphoreHandle_t mutex = nullptr;
  uint16_t* frontBuffer = nullptr;
  uint16_t* backBuffer = nullptr;
  String requestedUrl;
  String loadedUrl;
  bool loading = false;
  unsigned long lastAttemptAt = 0;
};

#include "AlbumArtCache.h"

#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace {
constexpr size_t kPixelCount = AlbumArtCache::SIZE * AlbumArtCache::SIZE;
constexpr size_t kPixelBytes = kPixelCount * sizeof(uint16_t);
constexpr size_t kMaximumJpegBytes = 512U * 1024U;
constexpr unsigned long kRetryDelayMs = 15000;

uint16_t* decodeDestination = nullptr;

bool copyDecodedBlock(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      uint16_t* pixels) {
  if (decodeDestination == nullptr) return false;
  // Blocks outside the destination can be skipped without aborting the JPEG
  // decoder. This also tolerates a CDN returning a slightly larger image.
  if (x >= AlbumArtCache::SIZE || y >= AlbumArtCache::SIZE) return true;
  const int16_t copyWidth = min<int16_t>(width, AlbumArtCache::SIZE - x);
  const int16_t copyHeight = min<int16_t>(height, AlbumArtCache::SIZE - y);
  for (int16_t row = 0; row < copyHeight; ++row) {
    memcpy(decodeDestination + (y + row) * AlbumArtCache::SIZE + x,
           pixels + row * width, copyWidth * sizeof(uint16_t));
  }
  return true;
}
}  // namespace

struct AlbumArtCache::DownloadRequest {
  AlbumArtCache* cache;
  String url;
};

AlbumArtCache& AlbumArtCache::instance() {
  static AlbumArtCache cache;
  return cache;
}

bool AlbumArtCache::ensureReady() {
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutex();
  }
  if (frontBuffer == nullptr) {
    frontBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (backBuffer == nullptr) {
    backBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return mutex != nullptr && frontBuffer != nullptr && backBuffer != nullptr;
}

void AlbumArtCache::request(const String& coverUrl, bool networkConnected) {
  if (!networkConnected || coverUrl.isEmpty() || !ensureReady()) return;

  xSemaphoreTake(mutex, portMAX_DELAY);
  if (requestedUrl != coverUrl) {
    requestedUrl = coverUrl;
    loadedUrl = "";
    lastError = "";
    lastAttemptAt = 0;
  }
  const unsigned long now = millis();
  const bool retryReady = lastAttemptAt == 0 || now - lastAttemptAt >= kRetryDelayMs;
  if (loading || loadedUrl == requestedUrl || !retryReady) {
    xSemaphoreGive(mutex);
    return;
  }
  loading = true;
  lastAttemptAt = now;
  xSemaphoreGive(mutex);

  DownloadRequest* taskRequest = new DownloadRequest{this, coverUrl};
  if (taskRequest == nullptr ||
      xTaskCreatePinnedToCore(downloadTask, "album-cover", 8192, taskRequest, 1,
                              nullptr, 0) != pdPASS) {
    delete taskRequest;
    xSemaphoreTake(mutex, portMAX_DELAY);
    loading = false;
    xSemaphoreGive(mutex);
  }
}

void AlbumArtCache::downloadTask(void* parameter) {
  DownloadRequest* request = static_cast<DownloadRequest*>(parameter);
  AlbumArtCache* cache = request->cache;
  const String url = request->url;
  delete request;

  String error;
  const bool success = cache->downloadAndDecode(url, error);
  xSemaphoreTake(cache->mutex, portMAX_DELAY);
  if (success && cache->requestedUrl == url) {
    uint16_t* swap = cache->frontBuffer;
    cache->frontBuffer = cache->backBuffer;
    cache->backBuffer = swap;
    cache->loadedUrl = url;
    ++cache->generation;
    Serial.printf("Album cover ready: %s\n", url.c_str());
  } else if (!success) {
    cache->lastError = error;
    Serial.printf("Album cover download/decode failed: %s\n", error.c_str());
  }
  cache->loading = false;
  xSemaphoreGive(cache->mutex);
  vTaskDelete(nullptr);
}

bool AlbumArtCache::downloadAndDecode(const String& sourceUrl, String& error) {
  String url = sourceUrl;
  url += url.indexOf('?') >= 0 ? "&param=128y128" : "?param=128y128";

  const bool secure = url.startsWith("https://");
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  if (secure) secureClient.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const bool began = secure ? http.begin(secureClient, url) : http.begin(plainClient, url);
  if (!began) { error = "http begin"; return false; }
  const int status = http.GET();
  if (status < 200 || status >= 300) {
    error = "http " + String(status);
    http.end();
    return false;
  }

  const int announcedLength = http.getSize();
  if (announcedLength > static_cast<int>(kMaximumJpegBytes)) {
    error = "image too large";
    http.end();
    return false;
  }
  const size_t capacity = announcedLength > 0
                              ? static_cast<size_t>(announcedLength)
                              : kMaximumJpegBytes;
  uint8_t* jpeg = static_cast<uint8_t*>(
      heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (jpeg == nullptr) {
    error = "image alloc";
    http.end();
    return false;
  }

  WiFiClient& stream = http.getStream();
  size_t received = 0;
  const unsigned long startedAt = millis();
  while (received < capacity && millis() - startedAt < 12000) {
    const int available = stream.available();
    if (available > 0) {
      const size_t chunk = min<size_t>(available, capacity - received);
      const int count = stream.readBytes(jpeg + received, chunk);
      if (count > 0) received += static_cast<size_t>(count);
    } else if (!http.connected()) {
      break;
    } else {
      delay(10);
    }
    if (announcedLength > 0 && received >= static_cast<size_t>(announcedLength)) break;
  }
  http.end();

  bool decoded = false;
  if (received == 0 || (announcedLength > 0 && received != static_cast<size_t>(announcedLength))) {
    error = "image incomplete";
  } else {
    memset(backBuffer, 0, kPixelBytes);
    decodeDestination = backBuffer;
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(copyDecodedBlock);
    uint16_t width = 0;
    uint16_t height = 0;
    if (TJpgDec.getJpgSize(&width, &height, jpeg, received) == JDR_OK && width > 0 && height > 0) {
      uint8_t scale = 1;
      while (scale < 8 &&
             (width / scale > SIZE || height / scale > SIZE)) {
        scale *= 2;
      }
      TJpgDec.setJpgScale(scale);
      decoded = TJpgDec.drawJpg(0, 0, jpeg, received) == JDR_OK;
      if (!decoded) error = "jpeg decode";
    } else {
      error = "jpeg size";
    }
    decodeDestination = nullptr;
  }
  free(jpeg);
  return decoded;
}

String AlbumArtCache::status() {
  if (!ensureReady()) return "memory unavailable";
  xSemaphoreTake(mutex, portMAX_DELAY);
  String result = loading ? "loading" : (!requestedUrl.isEmpty() && loadedUrl == requestedUrl)
      ? "ready" : (lastError.isEmpty() ? "idle" : "failed: " + lastError);
  xSemaphoreGive(mutex);
  return result;
}

bool AlbumArtCache::draw(Adafruit_GFX& display, int16_t x, int16_t y, int16_t size) {
  if (!ensureReady()) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool ready = !requestedUrl.isEmpty() && loadedUrl == requestedUrl;
  if (ready) {
    if (size == SIZE) {
      display.drawRGBBitmap(x, y, frontBuffer, SIZE, SIZE);
    } else {
      const int16_t outputSize = max<int16_t>(1, size);
      for (int16_t outputY = 0; outputY < outputSize; ++outputY) {
        const int16_t sourceY = outputY * SIZE / outputSize;
        for (int16_t outputX = 0; outputX < outputSize; ++outputX) {
          const int16_t sourceX = outputX * SIZE / outputSize;
          display.drawPixel(x + outputX, y + outputY, frontBuffer[sourceY * SIZE + sourceX]);
        }
      }
    }
  }
  xSemaphoreGive(mutex);
  return ready;
}

bool AlbumArtCache::drawBlurred(Adafruit_GFX& display, int16_t x, int16_t y, int16_t width,
                                int16_t height, int16_t cellSize,
                                uint8_t sourceOpacity, uint16_t background) {
  if (!ensureReady()) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool ready = !requestedUrl.isEmpty() && loadedUrl == requestedUrl;
  if (ready) {
    const int16_t cell = max<int16_t>(4, cellSize);
    for (int16_t top = 0; top < height; top += cell) {
      const int16_t sourceY = min<int16_t>(SIZE - 1, top * SIZE / max<int16_t>(1, height));
      const int16_t drawHeight = min<int16_t>(cell, height - top);
      for (int16_t left = 0; left < width; left += cell) {
        const int16_t sourceX = min<int16_t>(SIZE - 1, left * SIZE / max<int16_t>(1, width));
        const uint16_t source = frontBuffer[sourceY * SIZE + sourceX];
        const uint16_t inverse = 255 - sourceOpacity;
        const uint16_t red = (((background >> 11) & 0x1F) * inverse +
                              ((source >> 11) & 0x1F) * sourceOpacity + 127) / 255;
        const uint16_t green = (((background >> 5) & 0x3F) * inverse +
                                ((source >> 5) & 0x3F) * sourceOpacity + 127) / 255;
        const uint16_t blue = ((background & 0x1F) * inverse +
                               (source & 0x1F) * sourceOpacity + 127) / 255;
        display.fillRect(x + left, y + top, min<int16_t>(cell, width - left), drawHeight,
                         static_cast<uint16_t>((red << 11) | (green << 5) | blue));
      }
    }
  }
  xSemaphoreGive(mutex);
  return ready;
}

uint16_t AlbumArtCache::dominantColor(uint16_t fallback) {
  if (!ensureReady()) return fallback;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool ready = !requestedUrl.isEmpty() && loadedUrl == requestedUrl;
  if (!ready) {
    xSemaphoreGive(mutex);
    return fallback;
  }
  uint32_t red = 0;
  uint32_t green = 0;
  uint32_t blue = 0;
  constexpr int16_t kStep = 8;
  for (int16_t sampleY = 0; sampleY < SIZE; sampleY += kStep) {
    for (int16_t sampleX = 0; sampleX < SIZE; sampleX += kStep) {
      const uint16_t pixel = frontBuffer[sampleY * SIZE + sampleX];
      red += (pixel >> 11) & 0x1F;
      green += (pixel >> 5) & 0x3F;
      blue += pixel & 0x1F;
    }
  }
  constexpr uint16_t kSamples = (SIZE / kStep) * (SIZE / kStep);
  const uint16_t result = static_cast<uint16_t>(((red / kSamples) << 11) |
                                                 ((green / kSamples) << 5) |
                                                 (blue / kSamples));
  xSemaphoreGive(mutex);
  return result;
}

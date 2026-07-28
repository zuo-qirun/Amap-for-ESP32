#include "AlbumArtCache.h"

#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace {
constexpr size_t kPixelCount = AlbumArtCache::SIZE * AlbumArtCache::SIZE;
constexpr size_t kPixelBytes = kPixelCount * sizeof(uint16_t);
constexpr size_t kBackdropPixelCount =
    AlbumArtCache::BACKDROP_WIDTH * AlbumArtCache::BACKDROP_HEIGHT;
constexpr size_t kBackdropPixelBytes = kBackdropPixelCount * sizeof(uint16_t);
constexpr size_t kMaximumJpegBytes = 512U * 1024U;
constexpr unsigned long kRetryDelayMs = 15000;

uint16_t* decodeDestination = nullptr;

uint16_t blend565(uint16_t background, uint16_t source, uint8_t opacity) {
  const uint16_t inverse = 255 - opacity;
  const uint16_t red = (((background >> 11) & 0x1F) * inverse +
                        ((source >> 11) & 0x1F) * opacity + 127) / 255;
  const uint16_t green = (((background >> 5) & 0x3F) * inverse +
                          ((source >> 5) & 0x3F) * opacity + 127) / 255;
  const uint16_t blue = ((background & 0x1F) * inverse +
                         (source & 0x1F) * opacity + 127) / 255;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

bool buildBlurredBackdrop(const uint16_t* source, uint16_t* destination) {
  constexpr int16_t kLowWidth = 48;
  constexpr int16_t kLowHeight = 36;
  constexpr size_t kLowPixels = kLowWidth * kLowHeight;
  uint16_t* first = static_cast<uint16_t*>(
      heap_caps_malloc(kLowPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  uint16_t* second = static_cast<uint16_t*>(
      heap_caps_malloc(kLowPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (first == nullptr || second == nullptr) {
    free(first);
    free(second);
    return false;
  }

  // CSS background-size: cover for a square cover on a 4:3 screen: retain the
  // full width and crop 1/8 from the top and bottom before filtering.
  for (int16_t y = 0; y < kLowHeight; ++y) {
    const int16_t sourceY = 16 + y * 96 / kLowHeight;
    for (int16_t x = 0; x < kLowWidth; ++x) {
      const int16_t sourceX = x * AlbumArtCache::SIZE / kLowWidth;
      first[y * kLowWidth + x] = source[sourceY * AlbumArtCache::SIZE + sourceX];
    }
  }

  // Three small box passes approximate a Gaussian blur while keeping the
  // download task deterministic and avoiding a full-screen working buffer.
  for (uint8_t pass = 0; pass < 3; ++pass) {
    for (int16_t y = 0; y < kLowHeight; ++y) {
      for (int16_t x = 0; x < kLowWidth; ++x) {
        uint32_t red = 0, green = 0, blue = 0, samples = 0;
        for (int16_t dy = -2; dy <= 2; ++dy) {
          const int16_t sy = constrain(y + dy, 0, kLowHeight - 1);
          for (int16_t dx = -2; dx <= 2; ++dx) {
            const int16_t sx = constrain(x + dx, 0, kLowWidth - 1);
            const uint16_t pixel = first[sy * kLowWidth + sx];
            red += (pixel >> 11) & 0x1F;
            green += (pixel >> 5) & 0x3F;
            blue += pixel & 0x1F;
            ++samples;
          }
        }
        second[y * kLowWidth + x] = static_cast<uint16_t>(
            ((red / samples) << 11) | ((green / samples) << 5) | (blue / samples));
      }
    }
    uint16_t* swap = first;
    first = second;
    second = swap;
  }

  // Bilinear expansion removes the tile boundaries produced by the former
  // cell renderer; the TFT receives a continuous 320x240 RGB565 image.
  for (int16_t y = 0; y < AlbumArtCache::BACKDROP_HEIGHT; ++y) {
    const int32_t fy = y * (kLowHeight - 1) * 256 /
                       (AlbumArtCache::BACKDROP_HEIGHT - 1);
    const int16_t y0 = fy >> 8;
    const int16_t y1 = min<int16_t>(kLowHeight - 1, y0 + 1);
    const uint16_t wy = fy & 0xFF;
    for (int16_t x = 0; x < AlbumArtCache::BACKDROP_WIDTH; ++x) {
      const int32_t fx = x * (kLowWidth - 1) * 256 /
                         (AlbumArtCache::BACKDROP_WIDTH - 1);
      const int16_t x0 = fx >> 8;
      const int16_t x1 = min<int16_t>(kLowWidth - 1, x0 + 1);
      const uint16_t wx = fx & 0xFF;
      const uint16_t p00 = first[y0 * kLowWidth + x0];
      const uint16_t p10 = first[y0 * kLowWidth + x1];
      const uint16_t p01 = first[y1 * kLowWidth + x0];
      const uint16_t p11 = first[y1 * kLowWidth + x1];
      auto channel = [wx, wy](uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
        const uint32_t top = a * (256 - wx) + b * wx;
        const uint32_t bottom = c * (256 - wx) + d * wx;
        return static_cast<uint16_t>((top * (256 - wy) + bottom * wy + 32768) >> 16);
      };
      const uint16_t red = channel((p00 >> 11) & 0x1F, (p10 >> 11) & 0x1F,
                                   (p01 >> 11) & 0x1F, (p11 >> 11) & 0x1F);
      const uint16_t green = channel((p00 >> 5) & 0x3F, (p10 >> 5) & 0x3F,
                                     (p01 >> 5) & 0x3F, (p11 >> 5) & 0x3F);
      const uint16_t blue = channel(p00 & 0x1F, p10 & 0x1F, p01 & 0x1F, p11 & 0x1F);
      destination[y * AlbumArtCache::BACKDROP_WIDTH + x] =
          static_cast<uint16_t>((red << 11) | (green << 5) | blue);
    }
  }
  free(first);
  free(second);
  return true;
}

uint16_t extractPaletteColor(const uint16_t* source) {
  struct Bucket { uint16_t count = 0; uint32_t red = 0, green = 0, blue = 0; };
  Bucket buckets[256];
  for (int16_t y = 0; y < AlbumArtCache::SIZE; y += 2) {
    for (int16_t x = 0; x < AlbumArtCache::SIZE; x += 2) {
      const uint16_t pixel = source[y * AlbumArtCache::SIZE + x];
      const uint8_t red = ((pixel >> 11) & 0x1F) * 255 / 31;
      const uint8_t green = ((pixel >> 5) & 0x3F) * 255 / 63;
      const uint8_t blue = (pixel & 0x1F) * 255 / 31;
      const uint8_t key = (red >> 5) << 5 | (green >> 5) << 2 | (blue >> 6);
      Bucket& bucket = buckets[key];
      ++bucket.count;
      bucket.red += red;
      bucket.green += green;
      bucket.blue += blue;
    }
  }
  uint64_t bestScore = 0;
  uint16_t best = 0;
  for (const Bucket& bucket : buckets) {
    if (bucket.count == 0) continue;
    const uint16_t red = bucket.red / bucket.count;
    const uint16_t green = bucket.green / bucket.count;
    const uint16_t blue = bucket.blue / bucket.count;
    const uint16_t high = max<uint16_t>(red, max<uint16_t>(green, blue));
    const uint16_t low = min<uint16_t>(red, min<uint16_t>(green, blue));
    const uint16_t chroma = high - low;
    const uint16_t luminance = (red * 54 + green * 183 + blue * 19) >> 8;
    if (luminance < 12) continue;
    const uint16_t toneWeight = max<int>(48, 255 - abs(static_cast<int>(luminance) - 140));
    const uint64_t score = static_cast<uint64_t>(bucket.count) *
                           (chroma + 32) * toneWeight;
    if (score > bestScore) {
      bestScore = score;
      best = static_cast<uint16_t>(((red * 31 / 255) << 11) |
                                   ((green * 63 / 255) << 5) |
                                   (blue * 31 / 255));
    }
  }
  return best;
}

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
  if (frontBlurBuffer == nullptr) {
    frontBlurBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(kBackdropPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (backBlurBuffer == nullptr) {
    backBlurBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(kBackdropPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (styledBlurBuffer == nullptr) {
    styledBlurBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(kBackdropPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return mutex != nullptr && frontBuffer != nullptr && backBuffer != nullptr &&
         frontBlurBuffer != nullptr && backBlurBuffer != nullptr && styledBlurBuffer != nullptr;
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
  bool success = cache->downloadAndDecode(url, error);
  if (success) {
    success = buildBlurredBackdrop(cache->backBuffer, cache->backBlurBuffer);
    if (!success) error = "backdrop alloc";
    cache->backPaletteColor = extractPaletteColor(cache->backBuffer);
  }
  xSemaphoreTake(cache->mutex, portMAX_DELAY);
  if (success && cache->requestedUrl == url) {
    uint16_t* swap = cache->frontBuffer;
    cache->frontBuffer = cache->backBuffer;
    cache->backBuffer = swap;
    swap = cache->frontBlurBuffer;
    cache->frontBlurBuffer = cache->backBlurBuffer;
    cache->backBlurBuffer = swap;
    cache->frontPaletteColor = cache->backPaletteColor;
    cache->loadedUrl = url;
    ++cache->generation;
    cache->styledGeneration = UINT32_MAX;
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
    (void)cellSize;
    if (styledGeneration != generation || styledOpacity != sourceOpacity ||
        styledBackground != background) {
      for (size_t index = 0; index < kBackdropPixelCount; ++index) {
        styledBlurBuffer[index] = blend565(background, frontBlurBuffer[index], sourceOpacity);
      }
      styledGeneration = generation;
      styledOpacity = sourceOpacity;
      styledBackground = background;
    }
    if (width == BACKDROP_WIDTH && height == BACKDROP_HEIGHT) {
      display.drawRGBBitmap(x, y, styledBlurBuffer, BACKDROP_WIDTH, BACKDROP_HEIGHT);
    } else {
      for (int16_t outputY = 0; outputY < height; ++outputY) {
        const int16_t sourceY = outputY * BACKDROP_HEIGHT / max<int16_t>(1, height);
        for (int16_t outputX = 0; outputX < width; ++outputX) {
          const int16_t sourceX = outputX * BACKDROP_WIDTH / max<int16_t>(1, width);
          display.drawPixel(x + outputX, y + outputY,
                            styledBlurBuffer[sourceY * BACKDROP_WIDTH + sourceX]);
        }
      }
    }
  }
  xSemaphoreGive(mutex);
  return ready;
}

bool AlbumArtCache::drawBlurredRegion(Adafruit_GFX& display, int16_t x, int16_t y,
                                      int16_t width, int16_t height,
                                      uint8_t sourceOpacity, uint16_t background) {
  if (!ensureReady()) return false;
  const int16_t left = constrain(x, 0, BACKDROP_WIDTH);
  const int16_t top = constrain(y, 0, BACKDROP_HEIGHT);
  const int16_t right = constrain(x + width, 0, BACKDROP_WIDTH);
  const int16_t bottom = constrain(y + height, 0, BACKDROP_HEIGHT);
  if (right <= left || bottom <= top) return true;

  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool ready = !requestedUrl.isEmpty() && loadedUrl == requestedUrl;
  if (ready) {
    if (styledGeneration != generation || styledOpacity != sourceOpacity ||
        styledBackground != background) {
      for (size_t index = 0; index < kBackdropPixelCount; ++index) {
        styledBlurBuffer[index] = blend565(background, frontBlurBuffer[index], sourceOpacity);
      }
      styledGeneration = generation;
      styledOpacity = sourceOpacity;
      styledBackground = background;
    }
    const int16_t rowWidth = right - left;
    for (int16_t row = top; row < bottom; ++row) {
      display.drawRGBBitmap(left, row,
                            styledBlurBuffer + row * BACKDROP_WIDTH + left,
                            rowWidth, 1);
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
  const uint16_t result = frontPaletteColor == 0 ? fallback : frontPaletteColor;
  xSemaphoreGive(mutex);
  return result;
}

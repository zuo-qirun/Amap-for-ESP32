#include "TftRenderer.h"

#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <esp_heap_caps.h>

#include "Config.h"
#include "TftFrameRenderer.h"

namespace {
Adafruit_ST7789 tft(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
U8G2_FOR_ADAFRUIT_GFX tftFont;

constexpr size_t kPixels = AMAP_TFT_WIDTH * AMAP_TFT_HEIGHT;
constexpr size_t kPixelBytes = kPixels * sizeof(uint16_t);

void hashBytes(uint32_t& hash, const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
}

template <typename T>
void hashValue(uint32_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(value));
}

void hashString(uint32_t& hash, const String& value) {
  hashBytes(hash, value.c_str(), value.length());
  const uint8_t separator = 0xFF;
  hashValue(hash, separator);
}

uint32_t frameSignature(const NavState& state, bool wifiConnected, bool bleConnected,
                        const String& ip, uint16_t port, unsigned long silenceMs) {
  uint32_t hash = 2166136261UL;
  const bool connected = wifiConnected || bleConnected;
  const uint8_t screenState = !connected ? 0 : (!state.active || silenceMs > AMAP_STANDBY_MS)
                                               ? 1
                                               : (silenceMs > AMAP_STALE_MS ? 2 : 3);
  hashValue(hash, screenState);
  hashValue(hash, wifiConnected);
  hashValue(hash, bleConnected);
  hashString(hash, ip);
  hashValue(hash, port);
  if (screenState != 3) {
    return hash;
  }

  hashString(hash, state.mode);
  hashString(hash, state.road);
  hashValue(hash, state.turn.icon);
  hashString(hash, state.turn.distanceText);
  hashString(hash, state.turn.road);
  hashString(hash, state.eta.remainDistanceText);
  hashString(hash, state.eta.remainTimeText);
  hashString(hash, state.eta.arriveTimeText);
  hashValue(hash, state.speed.current);
  hashValue(hash, state.speed.limit);
  hashValue(hash, state.lane.count);
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    hashValue(hash, state.lane.lanes[i]);
    hashValue(hash, state.lane.advised[i]);
  }
  hashValue(hash, state.lightCount);
  for (uint8_t i = 0; i < state.lightCount; ++i) {
    hashValue(hash, state.lights[i].dir);
    hashValue(hash, state.lights[i].status);
    hashValue(hash, state.lights[i].seconds);
  }
  hashValue(hash, state.camera.type);
  hashValue(hash, state.camera.distance);
  hashValue(hash, state.camera.speedLimit);
  hashValue(hash, state.tmc.totalDistance);
  hashValue(hash, state.tmc.finishDistance);
  hashValue(hash, state.tmc.count);
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    hashValue(hash, state.tmc.status[i]);
    hashValue(hash, state.tmc.distance[i]);
  }
  hashValue(hash, state.route.remainingMeters);
  hashValue(hash, state.route.remainingSeconds);
  hashValue(hash, state.route.progressPercent);
  hashString(hash, state.route.destination);
  hashString(hash, state.guide.exitName);
  hashString(hash, state.guide.exitDirection);
  hashString(hash, state.guide.serviceAreaName);
  hashString(hash, state.guide.serviceAreaDistance);
  hashString(hash, state.guide.nextServiceAreaName);
  hashString(hash, state.guide.nextServiceAreaDistance);
  hashString(hash, state.alert);
  hashString(hash, state.detail);
  return hash;
}
}  // namespace

TftRenderer::Canvas::Canvas() : Adafruit_GFX(AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT) {}

TftRenderer::Canvas::~Canvas() {
  free(buffer);
}

bool TftRenderer::Canvas::begin() {
  if (buffer != nullptr) {
    return true;
  }
  buffer = static_cast<uint16_t*>(
      heap_caps_malloc(kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<uint16_t*>(malloc(kPixelBytes));
  }
  return buffer != nullptr;
}

uint16_t* TftRenderer::Canvas::pixels() {
  return buffer;
}

void TftRenderer::Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (buffer != nullptr && x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT) {
    buffer[y * WIDTH + x] = color;
  }
}

void TftRenderer::Canvas::drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) {
  if (buffer == nullptr || y < 0 || y >= HEIGHT || width <= 0) return;
  if (x < 0) { width += x; x = 0; }
  if (x + width > WIDTH) width = WIDTH - x;
  for (int16_t i = 0; i < width; ++i) buffer[y * WIDTH + x + i] = color;
}

void TftRenderer::Canvas::drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) {
  if (buffer == nullptr || x < 0 || x >= WIDTH || height <= 0) return;
  if (y < 0) { height += y; y = 0; }
  if (y + height > HEIGHT) height = HEIGHT - y;
  for (int16_t i = 0; i < height; ++i) buffer[(y + i) * WIDTH + x] = color;
}

void TftRenderer::Canvas::fillScreen(uint16_t color) {
  if (buffer == nullptr) return;
  for (size_t i = 0; i < kPixels; ++i) buffer[i] = color;
}

void TftRenderer::begin() {
  if (AMAP_TFT_SCLK_PIN < 0 || AMAP_TFT_MOSI_PIN < 0 || AMAP_TFT_CS_PIN < 0 ||
      AMAP_TFT_DC_PIN < 0 || AMAP_TFT_RST_PIN < 0 || AMAP_TFT_BL_PIN < 0) {
    Serial.println("TFT disabled: ST7789 pins are not configured");
    return;
  }

  pinMode(AMAP_TFT_BL_PIN, OUTPUT);
  digitalWrite(AMAP_TFT_BL_PIN, LOW);
  SPI.begin(AMAP_TFT_SCLK_PIN, AMAP_TFT_MISO_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
  tft.init(240, 320, SPI_MODE0);
  tft.setRotation(AMAP_TFT_ROTATION);
  tft.setSPISpeed(40000000);
  tft.invertDisplay(AMAP_TFT_INVERT_COLORS != 0);
  if (!canvas.begin() || !previousFrame.begin()) {
    Serial.println("TFT disabled: unable to allocate ST7789 frame buffers");
    return;
  }
  tft.fillScreen(0x0861);
  tftFont.begin(canvas);
  tftFont.setFontMode(1);
  tftFont.setFont(u8g2_font_wqy12_t_gb2312);
  digitalWrite(AMAP_TFT_BL_PIN, HIGH);
  ready = true;
  Serial.printf("ST7789 ready: %dx%d, SPI SCK=%d MOSI=%d CS=%d\n", tft.width(), tft.height(),
                AMAP_TFT_SCLK_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
}

bool TftRenderer::isReady() const {
  return ready;
}

void TftRenderer::render(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs) {
  if (!ready) {
    return;
  }
  const bool connected = wifiConnected || bleConnected;
  const uint32_t signature =
      frameSignature(state, wifiConnected, bleConnected, ip, port, silenceMs);
  if (frameDrawn && signature == lastFrameSignature) {
    return;
  }

  TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip, port,
                           silenceMs);
  uint16_t* current = canvas.pixels();
  uint16_t* previous = previousFrame.pixels();
  if (!frameDrawn) {
    tft.drawRGBBitmap(0, 0, current, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT);
    memcpy(previous, current, kPixelBytes);
  } else {
    // Static pixels remain untouched on the panel. For each scanline, transfer
    // only the span that differs from the last displayed frame. This keeps
    // distance/speed/time updates small without hard-coding layout rectangles.
    for (int16_t y = 0; y < AMAP_TFT_HEIGHT; ++y) {
      uint16_t* currentRow = current + y * AMAP_TFT_WIDTH;
      uint16_t* previousRow = previous + y * AMAP_TFT_WIDTH;
      if (memcmp(currentRow, previousRow, AMAP_TFT_WIDTH * sizeof(uint16_t)) == 0) {
        continue;
      }

      int16_t left = 0;
      while (left < AMAP_TFT_WIDTH && currentRow[left] == previousRow[left]) ++left;
      int16_t right = AMAP_TFT_WIDTH - 1;
      while (right > left && currentRow[right] == previousRow[right]) --right;
      const int16_t width = right - left + 1;
      tft.drawRGBBitmap(left, y, currentRow + left, width, 1);
      memcpy(previousRow + left, currentRow + left, width * sizeof(uint16_t));
    }
  }
  lastFrameSignature = signature;
  frameDrawn = true;
  (void)ip;
  (void)port;
}

#include "TftRenderer.h"

#include "Config.h"

#include <Adafruit_ILI9341.h>
#include <Adafruit_SPITFT.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <esp_heap_caps.h>

#include "HardwareSettings.h"
#include "TftFrameRenderer.h"

namespace {
Adafruit_ST7789 st7789(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
Adafruit_ILI9341 ili9341(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
Adafruit_SPITFT* activePanel = nullptr;
U8G2_FOR_ADAFRUIT_GFX tftFont;
U8G2_FOR_ADAFRUIT_GFX adjacentFont;

constexpr size_t kPixels = AMAP_TFT_WIDTH * AMAP_TFT_HEIGHT;
constexpr size_t kPixelBytes = kPixels * sizeof(uint16_t);
constexpr size_t kTransferPixels = 8U * 1024U;  // 16 KB internal DMA-capable staging
constexpr size_t kTransferBytes = kTransferPixels * sizeof(uint16_t);
constexpr int16_t kDirtyTileWidth = 16;
constexpr int16_t kDirtyTileHeight = 8;
constexpr int16_t kDirtyTileColumns = AMAP_TFT_WIDTH / kDirtyTileWidth;
constexpr int16_t kDirtyTileRows = AMAP_TFT_HEIGHT / kDirtyTileHeight;
constexpr size_t kDirtyTileCount = kDirtyTileColumns * kDirtyTileRows;
constexpr size_t kFullRefreshDirtyTiles = kDirtyTileCount * 45 / 100;
constexpr size_t kMaxDirtyRectangles = 96;

static_assert(AMAP_TFT_WIDTH % kDirtyTileWidth == 0,
              "TFT width must be divisible by dirty tile width");
static_assert(AMAP_TFT_HEIGHT % kDirtyTileHeight == 0,
              "TFT height must be divisible by dirty tile height");

struct DirtyRectangle {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

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

void hashMusic(uint32_t& hash, const MusicState& music, bool includePosition,
               unsigned long now) {
  hashValue(hash, music.active);
  hashValue(hash, music.playing);
  hashValue(hash, music.songId);
  hashString(hash, music.title);
  hashString(hash, music.artist);
  hashString(hash, music.album);
  hashString(hash, music.coverUrl);
  if (includePosition) {
    const int64_t positionFrame = music.positionAt(now) / 33;
    hashValue(hash, positionFrame);
  }
  hashValue(hash, music.durationMs);
  hashString(hash, music.previousLyric);
  hashString(hash, music.lyric);
  hashString(hash, music.translatedLyric);
  hashString(hash, music.nextLyric);
  hashString(hash, music.highlightedLyric);
  hashString(hash, music.currentWord);
  hashValue(hash, music.lineStartMs);
  hashValue(hash, music.lineDurationMs);
  hashValue(hash, music.wordStartMs);
  hashValue(hash, music.wordDurationMs);
  if (includePosition) {
    const int progressFrame = music.wordProgressAt(now);
    hashValue(hash, progressFrame);
  }
}

uint32_t frameSignature(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                         unsigned long now, TftViewMode viewMode,
                         int16_t dragOffsetX, bool showGestureHint) {
  uint32_t hash = 2166136261UL;
  const uint8_t view = static_cast<uint8_t>(viewMode);
  hashValue(hash, view);
  hashValue(hash, dragOffsetX);
  hashValue(hash, showGestureHint);
  const bool connected = wifiConnected || bleConnected;
  const uint8_t screenState = !connected ? 0
                              : silenceMs > AMAP_STANDBY_MS ? 1
                              : silenceMs > AMAP_STALE_MS ? 2
                              : state.active ? 3
                              : state.music.active ? 4 : 1;
  hashValue(hash, screenState);
  hashValue(hash, wifiConnected);
  hashValue(hash, bleConnected);
  hashString(hash, ip);
  hashValue(hash, port);
  if (screenState == 4) {
    hashMusic(hash, state.music, true, now);
    return hash;
  }
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
  if (state.music.active) {
    hashMusic(hash, state.music, true, now);
  }
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

TftRenderer::~TftRenderer() {
  free(transferBuffer);
}

void TftRenderer::begin() {
  if (AMAP_TFT_SCLK_PIN < 0 || AMAP_TFT_MOSI_PIN < 0 || AMAP_TFT_CS_PIN < 0 ||
      AMAP_TFT_DC_PIN < 0 || AMAP_TFT_RST_PIN < 0 || AMAP_TFT_BL_PIN < 0) {
    Serial.println("TFT disabled: SPI panel pins are not configured");
    return;
  }

  pinMode(AMAP_TFT_BL_PIN, OUTPUT);
  digitalWrite(AMAP_TFT_BL_PIN, LOW);
  SPI.begin(AMAP_TFT_SCLK_PIN, AMAP_TFT_MISO_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
  const HardwareSettings hardware = HardwareSettings::load();
  if (hardware.tftDriver == AMAP_TFT_DRIVER_ILI9341) {
    ili9341.begin(AMAP_TFT_SPI_FREQUENCY);
    ili9341.setRotation(AMAP_TFT_ROTATION);
    ili9341.setSPISpeed(AMAP_TFT_SPI_FREQUENCY);
    ili9341.invertDisplay(hardware.invertColors);
    activePanel = &ili9341;
  } else {
    st7789.init(AMAP_TFT_NATIVE_WIDTH, AMAP_TFT_NATIVE_HEIGHT, SPI_MODE0);
    st7789.setRotation(AMAP_TFT_ROTATION);
    st7789.setSPISpeed(AMAP_TFT_SPI_FREQUENCY);
    st7789.invertDisplay(hardware.invertColors);
    activePanel = &st7789;
  }
  transferBuffer = static_cast<uint16_t*>(heap_caps_malloc(
      kTransferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (transferBuffer == nullptr) {
    transferBuffer = static_cast<uint16_t*>(malloc(kTransferBytes));
  }
  if (!canvas.begin() || !previousFrame.begin() || !adjacentFrame.begin() ||
      transferBuffer == nullptr) {
    Serial.println("TFT disabled: unable to allocate frame buffers");
    return;
  }
  activePanel->fillScreen(0x0861);
  tftFont.begin(canvas);
  tftFont.setFontMode(1);
  tftFont.setFont(u8g2_font_wqy12_t_gb2312);
  adjacentFont.begin(adjacentFrame);
  adjacentFont.setFontMode(1);
  adjacentFont.setFont(u8g2_font_wqy12_t_gb2312);
  digitalWrite(AMAP_TFT_BL_PIN, HIGH);
  ready = true;
  const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("%s ready: %dx%d, inversion=%s, 3-frame buffered, PSRAM=%u/%u free, largest=%u, SPI SCK=%d MOSI=%d MISO=%d CS=%d\n",
                hardware.tftDriverName(), activePanel->width(), activePanel->height(),
                hardware.invertColors ? "on" : "off",
                static_cast<unsigned>(psramFree), static_cast<unsigned>(psramTotal),
                static_cast<unsigned>(psramLargest),
                AMAP_TFT_SCLK_PIN, AMAP_TFT_MOSI_PIN,
                AMAP_TFT_MISO_PIN, AMAP_TFT_CS_PIN);
}

bool TftRenderer::isReady() const {
  return ready;
}

void TftRenderer::updateTouch(uint8_t touchCount, int16_t x, int16_t y) {
  if (!ready) {
    return;
  }
  const unsigned long now = millis();
  if (touchCount > 1) {
    if (touching) {
      endTouch(now);
    }
    return;
  }
  if (touchCount > 0) {
    if (!touching) {
      beginTouch(x, y, now);
    } else {
      moveTouch(x, y, now);
    }
  } else if (touching) {
    endTouch(now);
  }
}

const char* TftRenderer::currentViewName() const {
  switch (viewMode) {
    case TftViewMode::Navigation: return "navigation";
    case TftViewMode::Music: return "music";
    case TftViewMode::Status: return "status";
    default: return "auto";
  }
}

void TftRenderer::beginTouch(int16_t x, int16_t y, unsigned long now) {
  touching = true;
  directionLocked = false;
  horizontalGesture = false;
  springActive = false;
  touchStartX = x;
  touchStartY = y;
  touchBaseOffset = dragOffsetX;
  lastSampleX = x;
  lastSampleY = y;
  releaseVelocityX = 0.0f;
  touchStartedAt = now;
  lastSampleAt = now;
  gestureHintUntil = now + 650UL;
}

void TftRenderer::moveTouch(int16_t x, int16_t y, unsigned long now) {
  const int16_t deltaX = x - touchStartX;
  const int16_t deltaY = y - touchStartY;
  if (!directionLocked && (abs(deltaX) >= 10 || abs(deltaY) >= 10)) {
    directionLocked = true;
    horizontalGesture = abs(deltaX) > abs(deltaY);
  }
  const unsigned long elapsed = now - lastSampleAt;
  if (elapsed > 0 && x != lastSampleX) {
    const float sampleVelocity = static_cast<float>(x - lastSampleX) * 1000.0f / elapsed;
    releaseVelocityX = releaseVelocityX * 0.55f + sampleVelocity * 0.45f;
  }
  if (directionLocked && horizontalGesture) {
    dragOffsetX = constrain(touchBaseOffset + deltaX,
                            -AMAP_TFT_WIDTH + 1, AMAP_TFT_WIDTH - 1);
    gestureHintUntil = now + 350UL;
  }
  lastSampleX = x;
  lastSampleY = y;
  lastSampleAt = now;
}

void TftRenderer::endTouch(unsigned long now) {
  const int16_t deltaY = lastSampleY - touchStartY;
  const unsigned long duration = now - touchStartedAt;
  touching = false;
  if (directionLocked && horizontalGesture) {
    const float projected = dragOffsetX + releaseVelocityX * 0.12f;
    const bool commit = abs(projected) >= 64;
    springTarget = commit ? (projected < 0 ? -AMAP_TFT_WIDTH : AMAP_TFT_WIDTH) : 0;
    springVelocity = releaseVelocityX;
    springActive = true;
    lastSpringAt = now;
  } else if (directionLocked && abs(deltaY) >= 42) {
    dragOffsetX = 0;
    switchView(deltaY < 0 ? TftViewMode::Status : TftViewMode::Auto, now);
  } else if (duration >= 650UL) {
    dragOffsetX = 0;
    switchView(TftViewMode::Auto, now);
  } else if (dragOffsetX != 0) {
    springTarget = abs(dragOffsetX) >= AMAP_TFT_WIDTH / 2
                       ? (dragOffsetX < 0 ? -AMAP_TFT_WIDTH : AMAP_TFT_WIDTH)
                       : 0;
    springVelocity = 0.0f;
    springActive = true;
    lastSpringAt = now;
  } else {
    gestureHintUntil = now + 1200UL;
  }
}

void TftRenderer::advanceSpring(unsigned long now) {
  if (!springActive || touching) {
    return;
  }
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) {
    return;
  }
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  const float position = dragOffsetX;
  constexpr float omega = 19.0f;
  const float acceleration = omega * omega * (springTarget - position) -
                             2.0f * omega * springVelocity;
  springVelocity += acceleration * dt;
  float next = position + springVelocity * dt;
  if ((springTarget > 0 && next > springTarget) ||
      (springTarget < 0 && next < springTarget)) {
    next = springTarget;
    springVelocity = 0.0f;
  }
  dragOffsetX = static_cast<int16_t>(roundf(next));
  if (abs(springTarget - dragOffsetX) <= 1 && abs(springVelocity) < 12.0f) {
    dragOffsetX = springTarget;
    if (springTarget == 0) {
      springActive = false;
      springVelocity = 0.0f;
      gestureHintUntil = now + 700UL;
    } else {
      finishHorizontalTransition();
    }
  }
}

void TftRenderer::switchView(TftViewMode mode, unsigned long now) {
  viewMode = mode;
  springActive = false;
  springVelocity = 0.0f;
  springTarget = 0;
  gestureHintUntil = now + 1200UL;
  Serial.printf("touch view: %s\n", currentViewName());
}

void TftRenderer::finishHorizontalTransition() {
  const int direction = springTarget < 0 ? 1 : -1;
  viewMode = adjacentView(direction);
  dragOffsetX = 0;
  springTarget = 0;
  springVelocity = 0.0f;
  springActive = false;
  gestureHintUntil = millis() + 1000UL;
  Serial.printf("touch view: %s\n", currentViewName());
}

TftViewMode TftRenderer::adjacentView(int direction) const {
  const int count = 4;
  const int current = static_cast<int>(viewMode);
  return static_cast<TftViewMode>((current + direction + count) % count);
}

void TftRenderer::compositeHorizontalSlide(int16_t offset) {
  if (offset == 0) {
    return;
  }
  const int16_t shift = min<int16_t>(abs(offset), AMAP_TFT_WIDTH - 1);
  uint16_t* current = canvas.pixels();
  const uint16_t* adjacent = adjacentFrame.pixels();
  for (int16_t row = 0; row < AMAP_TFT_HEIGHT; ++row) {
    uint16_t* currentRow = current + row * AMAP_TFT_WIDTH;
    const uint16_t* adjacentRow = adjacent + row * AMAP_TFT_WIDTH;
    if (offset < 0) {
      memmove(currentRow, currentRow + shift,
              (AMAP_TFT_WIDTH - shift) * sizeof(uint16_t));
      memcpy(currentRow + AMAP_TFT_WIDTH - shift, adjacentRow,
             shift * sizeof(uint16_t));
    } else {
      memmove(currentRow + shift, currentRow,
              (AMAP_TFT_WIDTH - shift) * sizeof(uint16_t));
      memcpy(currentRow, adjacentRow + AMAP_TFT_WIDTH - shift,
             shift * sizeof(uint16_t));
    }
  }
}

void TftRenderer::render(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs) {
  if (!ready) {
    return;
  }
  const bool connected = wifiConnected || bleConnected;
  const unsigned long now = millis();
  advanceSpring(now);
  const bool showGestureHint = touching || springActive ||
                               static_cast<long>(gestureHintUntil - now) > 0;
  const uint32_t signature =
      frameSignature(state, wifiConnected, bleConnected, ip, port, silenceMs, now,
                     viewMode, dragOffsetX, showGestureHint);
  if (frameDrawn && signature == lastFrameSignature) {
    return;
  }

  const uint32_t frameStartedAt = micros();
  TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip, port,
                           silenceMs, viewMode);
  if (dragOffsetX != 0) {
    const int direction = dragOffsetX < 0 ? 1 : -1;
    TftFrameRenderer::render(adjacentFrame, adjacentFont, state, wifiConnected, bleConnected,
                             ip, port, silenceMs, adjacentView(direction));
    compositeHorizontalSlide(dragOffsetX);
  }
  if (showGestureHint) {
    TftFrameRenderer::drawGestureHint(canvas, tftFont, viewMode);
  }
  const uint32_t composedAt = micros();
  uint16_t* current = canvas.pixels();
  uint16_t* previous = previousFrame.pixels();
  if (!frameDrawn) {
    const uint32_t transferStartedAt = micros();
    pushRectangle(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT,
                  current, AMAP_TFT_WIDTH);
    memcpy(previous, current, kPixelBytes);
    Serial.printf("TFT full frame: compose=%lu ms transfer=%lu ms total=%lu ms\n",
                  static_cast<unsigned long>((composedAt - frameStartedAt) / 1000),
                  static_cast<unsigned long>((micros() - transferStartedAt) / 1000),
                  static_cast<unsigned long>((micros() - frameStartedAt) / 1000));
  } else {
    // Detect changes in small tiles, then merge identical horizontal runs on
    // adjacent tile rows. The third PSRAM buffer packs each merged rectangle
    // into contiguous memory, avoiding one SPI transaction per scanline.
    bool dirtyTiles[kDirtyTileRows][kDirtyTileColumns] = {};
    size_t dirtyTileCount = 0;
    for (int16_t tileY = 0; tileY < kDirtyTileRows; ++tileY) {
      for (int16_t tileX = 0; tileX < kDirtyTileColumns; ++tileX) {
        const int16_t x = tileX * kDirtyTileWidth;
        const int16_t y = tileY * kDirtyTileHeight;
        bool dirty = false;
        for (int16_t row = 0; row < kDirtyTileHeight && !dirty; ++row) {
          const size_t offset = (y + row) * AMAP_TFT_WIDTH + x;
          dirty = memcmp(current + offset, previous + offset,
                         kDirtyTileWidth * sizeof(uint16_t)) != 0;
        }
        dirtyTiles[tileY][tileX] = dirty;
        dirtyTileCount += dirty ? 1 : 0;
      }
    }

    DirtyRectangle rectangles[kMaxDirtyRectangles];
    size_t rectangleCount = 0;
    bool tooFragmented = false;
    for (int16_t tileY = 0; tileY < kDirtyTileRows && !tooFragmented; ++tileY) {
      int16_t tileX = 0;
      while (tileX < kDirtyTileColumns) {
        while (tileX < kDirtyTileColumns && !dirtyTiles[tileY][tileX]) ++tileX;
        if (tileX >= kDirtyTileColumns) break;
        const int16_t runStart = tileX;
        while (tileX < kDirtyTileColumns && dirtyTiles[tileY][tileX]) ++tileX;
        const int16_t runWidth = (tileX - runStart) * kDirtyTileWidth;
        const int16_t runX = runStart * kDirtyTileWidth;
        const int16_t runY = tileY * kDirtyTileHeight;

        bool extended = false;
        for (size_t i = 0; i < rectangleCount; ++i) {
          DirtyRectangle& rectangle = rectangles[i];
          if (rectangle.x == runX && rectangle.width == runWidth &&
              rectangle.y + rectangle.height == runY) {
            rectangle.height += kDirtyTileHeight;
            extended = true;
            break;
          }
        }
        if (!extended) {
          if (rectangleCount >= kMaxDirtyRectangles) {
            tooFragmented = true;
            break;
          }
          rectangles[rectangleCount++] = {
              runX, runY, runWidth, kDirtyTileHeight};
        }
      }
    }

    if (tooFragmented || dirtyTileCount >= kFullRefreshDirtyTiles) {
      const uint32_t transferStartedAt = micros();
      pushRectangle(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT,
                    current, AMAP_TFT_WIDTH);
      memcpy(previous, current, kPixelBytes);
      Serial.printf("TFT full refresh: dirty=%u/%u compose=%lu ms transfer=%lu ms total=%lu ms\n",
                    static_cast<unsigned>(dirtyTileCount),
                    static_cast<unsigned>(kDirtyTileCount),
                    static_cast<unsigned long>((composedAt - frameStartedAt) / 1000),
                    static_cast<unsigned long>((micros() - transferStartedAt) / 1000),
                    static_cast<unsigned long>((micros() - frameStartedAt) / 1000));
    } else {
      for (size_t i = 0; i < rectangleCount; ++i) {
        const DirtyRectangle& rectangle = rectangles[i];
        for (int16_t row = 0; row < rectangle.height; ++row) {
          const size_t sourceOffset =
              (rectangle.y + row) * AMAP_TFT_WIDTH + rectangle.x;
          memcpy(previous + sourceOffset,
                 current + sourceOffset,
                 rectangle.width * sizeof(uint16_t));
        }
        pushRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                      current + rectangle.y * AMAP_TFT_WIDTH + rectangle.x,
                      AMAP_TFT_WIDTH);
      }
    }
  }
  lastFrameSignature = signature;
  frameDrawn = true;
  (void)ip;
  (void)port;
}

void TftRenderer::pushRectangle(int16_t x, int16_t y, int16_t width,
                                int16_t height, const uint16_t* source,
                                int16_t sourceStride) {
  if (activePanel == nullptr || transferBuffer == nullptr || width <= 0 || height <= 0) {
    return;
  }
  const int16_t rowsPerChunk = max<int16_t>(1, kTransferPixels / width);
  activePanel->startWrite();
  activePanel->setAddrWindow(x, y, width, height);
  for (int16_t row = 0; row < height;) {
    const int16_t chunkRows = min<int16_t>(rowsPerChunk, height - row);
    for (int16_t chunkRow = 0; chunkRow < chunkRows; ++chunkRow) {
      memcpy(transferBuffer + chunkRow * width,
             source + (row + chunkRow) * sourceStride,
             width * sizeof(uint16_t));
    }
    activePanel->writePixels(transferBuffer,
                             static_cast<uint32_t>(chunkRows) * width,
                             true, false);
    row += chunkRows;
  }
  activePanel->endWrite();
}

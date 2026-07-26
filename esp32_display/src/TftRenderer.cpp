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
  hashString(hash, music.source);
  hashString(hash, music.sourceName);
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

void hashWeather(uint32_t& hash, const WeatherState& weather) {
  hashValue(hash, weather.configured);
  hashValue(hash, weather.loading);
  hashValue(hash, weather.valid);
  hashString(hash, weather.city);
  hashString(hash, weather.timezone);
  hashString(hash, weather.condition);
  hashString(hash, weather.error);
  hashValue(hash, weather.temperatureC);
  hashValue(hash, weather.feelsLikeC);
  hashValue(hash, weather.windKph);
  hashValue(hash, weather.humidity);
  hashValue(hash, weather.weatherCode);
  hashValue(hash, weather.airQualityValid);
  hashValue(hash, weather.usAqi);
  hashValue(hash, weather.pm25);
  hashValue(hash, weather.pm10);
  hashString(hash, weather.airQualityError);
  hashValue(hash, weather.isDay);
  for (const WeatherForecastDay& day : weather.days) {
    hashString(hash, day.date);
    hashValue(hash, day.weatherCode);
    hashValue(hash, day.highC);
    hashValue(hash, day.lowC);
    hashValue(hash, day.rainChance);
  }
}

uint32_t frameSignature(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                          unsigned long now, TftViewMode viewMode,
                          int16_t dragOffsetX, bool showGestureHint,
                          MediaControlCommand pressedControl, int8_t pressedSettingsRow,
                          bool phoneDetail, uint8_t phoneDetailScroll,
                          bool phoneSheetVisible, int16_t phoneSheetOffsetY,
                          bool automaticMode, bool homeTransitionVisible,
                          int16_t homeTransitionOffsetY, uint8_t settingsPage,
                          int16_t homeScroll, int16_t musicLyricOffsetY,
                          const WeatherState& weather) {
  uint32_t hash = 2166136261UL;
  const uint8_t view = static_cast<uint8_t>(viewMode);
  hashValue(hash, view);
  hashValue(hash, dragOffsetX);
  hashValue(hash, showGestureHint);
  const uint8_t pressed = static_cast<uint8_t>(pressedControl);
  hashValue(hash, pressed);
  hashValue(hash, pressedSettingsRow);
  hashValue(hash, phoneDetail);
  hashValue(hash, phoneDetailScroll);
  hashValue(hash, phoneSheetVisible);
  hashValue(hash, phoneSheetOffsetY);
  hashValue(hash, automaticMode);
  hashValue(hash, homeTransitionVisible);
  hashValue(hash, homeTransitionOffsetY);
  hashValue(hash, settingsPage);
  hashValue(hash, homeScroll);
  hashValue(hash, musicLyricOffsetY);
  hashValue(hash, state.phone.enabled);
  hashValue(hash, state.phone.notification.active);
  hashString(hash, state.phone.notification.kind);
  hashString(hash, state.phone.notification.app);
  hashString(hash, state.phone.notification.sender);
  hashString(hash, state.phone.notification.title);
  hashString(hash, state.phone.notification.body);
  hashValue(hash, state.phone.weather.temperatureC);
  hashString(hash, state.phone.weather.condition);
  hashValue(hash, state.phone.weather.aqi);
  hashString(hash, state.phone.calendar.title);
  hashString(hash, state.phone.calendar.location);
  hashValue(hash, state.phone.device.batteryPercent);
  hashValue(hash, state.phone.device.charging);
  hashString(hash, state.phone.device.network);
  hashWeather(hash, weather);
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

  ledcSetup(0, 12000, 8);
  ledcAttachPin(AMAP_TFT_BL_PIN, 0);
  displayPreferences = DisplayPreferences::load();
  applyBrightness();
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
      !homeTransitionFrame.begin() ||
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
  applyBrightness();
  ready = true;
  const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("%s ready: %dx%d, inversion=%s, 4-frame buffered, PSRAM=%u/%u free, largest=%u, SPI SCK=%d MOSI=%d CS=%d\n",
                hardware.tftDriverName(), activePanel->width(), activePanel->height(),
                hardware.invertColors ? "on" : "off",
                static_cast<unsigned>(psramFree), static_cast<unsigned>(psramTotal),
                static_cast<unsigned>(psramLargest),
                AMAP_TFT_SCLK_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
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

bool TftRenderer::takeMediaControlCommand(MediaControlCommand& command) {
  if (pendingMediaControl == MediaControlCommand::None) {
    return false;
  }
  command = pendingMediaControl;
  pendingMediaControl = MediaControlCommand::None;
  return true;
}

const char* TftRenderer::currentViewName() const {
  switch (viewMode) {
    case TftViewMode::Home: return "home";
    case TftViewMode::Navigation: return "navigation";
    case TftViewMode::Music: return "music";
    case TftViewMode::Weather: return "weather";
    case TftViewMode::Settings: return "settings";
    case TftViewMode::AutoStatus: return "auto_status";
    default: return "auto";
  }
}

void TftRenderer::beginTouch(int16_t x, int16_t y, unsigned long now) {
  touching = true;
  directionLocked = false;
  horizontalGesture = false;
  horizontalBlocked = false;
  phoneSheetGesture = false;
  phoneSheetAnimationLocked = phoneSheetSpringActive;
  homeGesture = false;
  homeScrollGesture = false;
  musicLyricGesture = false;
  springActive = false;
  touchStartX = x;
  touchStartY = y;
  touchBaseOffset = dragOffsetX;
  homeScrollStart = homeScroll;
  musicLyricOffsetStart = musicLyricOffsetY;
  lastSampleX = x;
  lastSampleY = y;
  releaseVelocityX = 0.0f;
  releaseVelocityY = 0.0f;
  touchStartedAt = now;
  lastSampleAt = now;
  gestureHintUntil = now + 650UL;
  pressedMediaControl = musicControlsVisible ? hitTestMediaControl(x, y)
                                             : MediaControlCommand::None;
  settingsRow = -1;
  autoStatusPressed = viewMode == TftViewMode::AutoStatus && x >= 12 && x <= 308 &&
                      y >= 177 && y <= 220;
  if (viewMode == TftViewMode::Settings) {
    if (settingsPage > 0 && x >= 12 && x <= 66 && y >= 10 && y <= 35) {
      settingsRow = -2;
    } else if (settingsPage == 0 && x >= 8 && x <= 312 && y >= 58 && y <= 201) {
      settingsRow = static_cast<int8_t>((y - 58) / 51);
    } else if ((settingsPage == 1 || settingsPage == 2) &&
               x >= 8 && x <= 312 && y >= 70 && y <= 147) {
      settingsRow = static_cast<int8_t>((y - 70) / 42);
    }
  }
}

void TftRenderer::moveTouch(int16_t x, int16_t y, unsigned long now) {
  const int16_t deltaX = x - touchStartX;
  const int16_t deltaY = y - touchStartY;
  if (!directionLocked && (abs(deltaX) >= 10 || abs(deltaY) >= 10)) {
    directionLocked = true;
    // Horizontal swipes intentionally have no navigation role. Reserve the
    // vertical edges for the two system gestures, then let the desktop list
    // consume ordinary vertical drags.
    horizontalGesture = false;
    phoneSheetGesture = !phoneSheetAnimationLocked &&
                        (phoneSheetVisible || (touchStartY <= 60 && deltaY > 0 &&
                                               abs(deltaY) >= abs(deltaX)));
    homeGesture = !phoneSheetGesture && !phoneSheetVisible && viewMode != TftViewMode::Home &&
                  touchStartY >= AMAP_TFT_HEIGHT - 42 && deltaY < 0;
    musicLyricGesture = !phoneSheetGesture && !homeGesture && viewMode == TftViewMode::Music &&
                        touchStartX >= 150 && touchStartY >= 34 && touchStartY <= 180 &&
                        abs(deltaY) >= abs(deltaX);
    homeScrollGesture = !phoneSheetGesture && !homeGesture && !musicLyricGesture &&
                        viewMode == TftViewMode::Home &&
                        abs(deltaY) >= abs(deltaX);
    // The notification sheet settles once released; a later touch may not
    // grab it mid-flight. The desktop return remains directly manipulable.
    if (homeGesture) homeTransitionSpringActive = false;
    if (homeGesture && !homeTransitionSnapshotReady) {
      memcpy(homeTransitionFrame.pixels(), previousFrame.pixels(), kPixelBytes);
      homeTransitionSnapshotReady = true;
    }
  }
  if (directionLocked) {
    pressedMediaControl = MediaControlCommand::None;
  }
  const unsigned long elapsed = now - lastSampleAt;
  if (elapsed > 0 && x != lastSampleX) {
    const float sampleVelocity = static_cast<float>(x - lastSampleX) * 1000.0f / elapsed;
    releaseVelocityX = releaseVelocityX * 0.55f + sampleVelocity * 0.45f;
  }
  if (elapsed > 0 && y != lastSampleY) {
    const float sampleVelocity = static_cast<float>(y - lastSampleY) * 1000.0f / elapsed;
    releaseVelocityY = releaseVelocityY * 0.55f + sampleVelocity * 0.45f;
  }
  if (directionLocked && phoneSheetGesture) {
    const int16_t rawOffset = phoneSheetVisible
        ? deltaY : static_cast<int16_t>(-AMAP_TFT_HEIGHT + max<int16_t>(0, deltaY));
    // Resist a pull beyond the open/closed endpoints instead of hard-stopping.
    if (rawOffset > 0) {
      phoneSheetOffsetY = rawOffset / 3;
    } else if (rawOffset < -AMAP_TFT_HEIGHT) {
      phoneSheetOffsetY = -AMAP_TFT_HEIGHT + (rawOffset + AMAP_TFT_HEIGHT) / 3;
    } else {
      phoneSheetOffsetY = rawOffset;
    }
    phoneSheetVisible = true;
    gestureHintUntil = now + 350UL;
  } else if (directionLocked && homeGesture) {
    const int16_t rawOffset = static_cast<int16_t>(AMAP_TFT_HEIGHT + min<int16_t>(0, deltaY));
    homeTransitionOffsetY = max<int16_t>(0, rawOffset);
    homeTransitionVisible = true;
    gestureHintUntil = now + 350UL;
  } else if (directionLocked && musicLyricGesture) {
    musicLyricOffsetY = constrain(musicLyricOffsetStart + deltaY, -36, 36);
    musicLyricReturnAt = 0;
    frameDrawn = false;
  } else if (directionLocked && homeScrollGesture) {
    homeScroll = constrain(homeScrollStart - deltaY, 0, 33);
    frameDrawn = false;
  }
  lastSampleX = x;
  lastSampleY = y;
  lastSampleAt = now;
}

void TftRenderer::endTouch(unsigned long now) {
  const int16_t deltaY = lastSampleY - touchStartY;
  const unsigned long duration = now - touchStartedAt;
  touching = false;
  if (!directionLocked && pressedMediaControl != MediaControlCommand::None &&
      hitTestMediaControl(lastSampleX, lastSampleY) == pressedMediaControl &&
      duration < 700UL) {
    pendingMediaControl = pressedMediaControl;
    Serial.printf("touch media control: %s\n", mediaControlAction(pendingMediaControl));
    pressedMediaControl = MediaControlCommand::None;
    gestureHintUntil = 0;
    return;
  }
  if (!directionLocked && settingsRow >= 0 && duration < 700UL && viewMode == TftViewMode::Settings) {
    cycleSettingsRow();
    settingsRow = -1;
    return;
  }
  if (!directionLocked && settingsRow == -2 && duration < 700UL &&
      viewMode == TftViewMode::Settings) {
    settingsPage = 0;
    settingsRow = -1;
    frameDrawn = false;
    return;
  }
  if (!directionLocked && autoStatusPressed && duration < 700UL &&
      viewMode == TftViewMode::AutoStatus && lastSampleY >= 177 && lastSampleY <= 220) {
    displayPreferences.autoView = !displayPreferences.autoView;
    displayPreferences.save();
    // "Auto" is its own status app. Changing the background preference must
    // not replace this screen with the navigation renderer.
    frameDrawn = false;
    return;
  }
  if (!directionLocked && duration < 700UL && phoneSheetVisible && lastSampleY >= 195) {
    phoneDetail = !phoneDetail;
    phoneDetailScroll = 0;
    frameDrawn = false;
    return;
  }
  pressedMediaControl = MediaControlCommand::None;
  if (directionLocked && phoneSheetGesture) {
    if (phoneDetail && touchStartY > 42 && abs(deltaY) >= 28 && phoneSheetOffsetY == 0) {
      phoneDetailScroll = constrain(static_cast<int>(phoneDetailScroll) + (deltaY < 0 ? 3 : -3), 0, 24);
      frameDrawn = false;
      return;
    }
    constexpr int16_t kEdgeCommitDistance = 60;
    const bool open = phoneSheetOffsetY >= -AMAP_TFT_HEIGHT + kEdgeCommitDistance ||
                      releaseVelocityY > 360.0f;
    const bool close = phoneSheetOffsetY <= -kEdgeCommitDistance || releaseVelocityY < -360.0f;
    phoneSheetTargetY = phoneSheetOffsetY > -AMAP_TFT_HEIGHT / 2
                            ? (close ? -AMAP_TFT_HEIGHT : 0)
                            : (open ? 0 : -AMAP_TFT_HEIGHT);
    phoneSheetVelocity = releaseVelocityY;
    phoneSheetSpringActive = true;
    lastSpringAt = now;
  } else if (directionLocked && homeGesture) {
    constexpr int16_t kHomeCommitDistance = 60;
    const bool returnHome = homeTransitionOffsetY <= AMAP_TFT_HEIGHT - kHomeCommitDistance ||
                            releaseVelocityY < -360.0f;
    homeTransitionTargetY = returnHome ? 0 : AMAP_TFT_HEIGHT;
    homeTransitionVelocity = releaseVelocityY;
    homeTransitionSpringActive = true;
    lastSpringAt = now;
  } else if (directionLocked && musicLyricGesture) {
    musicLyricReturnAt = now + 2000UL;
    lastMusicLyricFrameAt = now;
    frameDrawn = false;
  } else if (directionLocked && homeScrollGesture) {
    frameDrawn = false;
  } else if (directionLocked && !phoneSheetAnimationLocked && !horizontalGesture &&
             phoneSheetVisible && phoneDetail) {
    phoneDetailScroll = constrain(static_cast<int>(phoneDetailScroll) + (deltaY < 0 ? 3 : -3), 0, 24);
    frameDrawn = false;
  } else if (!directionLocked && duration < 700UL && viewMode == TftViewMode::Home) {
    const int16_t contentY = lastSampleY + homeScroll;
    if (contentY >= 201 && contentY <= 263) {
      switchView(TftViewMode::Settings, now);
      settingsPage = lastSampleX < 160 ? 3 : 1;
    } else {
      const TftViewMode app = hitTestHomeApp(lastSampleX, contentY);
      if (app != TftViewMode::Home) switchView(app, now);
    }
  } else if (directionLocked && abs(lastSampleX - touchStartX) > abs(deltaY)) {
    // Horizontal drags deliberately do not navigate or reveal another view.
    gestureHintUntil = 0;
  } else {
    gestureHintUntil = now + 1200UL;
  }
}

void TftRenderer::applyBrightness() {
  const uint32_t duty = static_cast<uint32_t>(displayPreferences.effectiveBrightness()) * 255U / 100U;
  ledcWrite(0, duty);
}

void TftRenderer::cycleSettingsRow() {
  if (settingsPage == 0) {
    settingsPage = constrain(static_cast<int>(settingsRow) + 1, 1, 3);
    frameDrawn = false;
    return;
  }
  const int row = constrain(static_cast<int>(settingsRow), 0, 1);
  if (settingsPage == 1) {
    if (row == 0) displayPreferences.brightness = displayPreferences.brightness >= 100
        ? 20 : displayPreferences.brightness + 20;
    else displayPreferences.nightDim = !displayPreferences.nightDim;
  } else if (settingsPage == 2) {
    if (row == 0) {
      displayPreferences.autoView = !displayPreferences.autoView;
      if (!displayPreferences.autoView && viewMode == TftViewMode::Auto) viewMode = TftViewMode::Home;
    } else {
      displayPreferences.messageBanners = !displayPreferences.messageBanners;
    }
  }
  displayPreferences.save();
  applyBrightness();
  frameDrawn = false;
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

void TftRenderer::advancePhoneSheetSpring(unsigned long now) {
  if (!phoneSheetSpringActive) {
    return;
  }
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) return;
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  constexpr float omega = 20.0f;  // response ~0.35 s, critically damped
  const float position = phoneSheetOffsetY;
  const float acceleration = omega * omega * (phoneSheetTargetY - position) -
                             2.0f * omega * phoneSheetVelocity;
  phoneSheetVelocity += acceleration * dt;
  float next = position + phoneSheetVelocity * dt;
  if ((phoneSheetTargetY == 0 && next > 0) ||
      (phoneSheetTargetY == -AMAP_TFT_HEIGHT && next < -AMAP_TFT_HEIGHT)) {
    next = phoneSheetTargetY;
    phoneSheetVelocity = 0.0f;
  }
  phoneSheetOffsetY = static_cast<int16_t>(roundf(next));
  if (abs(phoneSheetTargetY - phoneSheetOffsetY) <= 1 && abs(phoneSheetVelocity) < 12.0f) {
    phoneSheetOffsetY = phoneSheetTargetY;
    phoneSheetSpringActive = false;
    phoneSheetVelocity = 0.0f;
    phoneSheetVisible = phoneSheetOffsetY > -AMAP_TFT_HEIGHT;
    gestureHintUntil = now + 700UL;
  }
}

void TftRenderer::advanceHomeTransitionSpring(unsigned long now) {
  if (!homeTransitionSpringActive || touching) return;
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) return;
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  // A critically damped analytic spring keeps the app exit brisk without the
  // long tail or numerical wobble that a larger Euler step would introduce.
  constexpr float omega = 28.0f;
  const float position = homeTransitionOffsetY;
  const float displacement = position - homeTransitionTargetY;
  const float c = homeTransitionVelocity + omega * displacement;
  const float decay = expf(-omega * dt);
  const float next = homeTransitionTargetY + (displacement + c * dt) * decay;
  homeTransitionVelocity = (homeTransitionVelocity - omega * c * dt) * decay;
  homeTransitionOffsetY = static_cast<int16_t>(roundf(next));
  if (abs(homeTransitionTargetY - homeTransitionOffsetY) <= 1 &&
      abs(homeTransitionVelocity) < 12.0f) {
    const bool commit = homeTransitionTargetY == 0;
    homeTransitionOffsetY = AMAP_TFT_HEIGHT;
    homeTransitionSpringActive = false;
    homeTransitionVisible = false;
    homeTransitionSnapshotReady = false;
    homeTransitionVelocity = 0.0f;
    if (commit) viewMode = TftViewMode::Home;
    gestureHintUntil = now + 700UL;
  }
}

void TftRenderer::switchView(TftViewMode mode, unsigned long now) {
  viewMode = mode;
  if (mode == TftViewMode::Settings) settingsPage = 0;
  springActive = false;
  homeTransitionSpringActive = false;
  homeTransitionVisible = false;
  homeTransitionSnapshotReady = false;
  homeTransitionOffsetY = AMAP_TFT_HEIGHT;
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
  const TftViewMode pages[] = {TftViewMode::Home, TftViewMode::Navigation,
                               TftViewMode::Music, TftViewMode::Settings};
  int index = 0;
  for (uint8_t candidate = 0; candidate < 4; ++candidate) {
    if (pages[candidate] == viewMode) {
      index = candidate;
      break;
    }
  }
  return pages[constrain(index + direction, 0, 3)];
}

TftViewMode TftRenderer::resolveAutoView(const NavState& state, bool connected,
                                         unsigned long silenceMs) const {
  if (!connected || silenceMs > AMAP_STALE_MS) return TftViewMode::Home;
  if (state.active) return TftViewMode::Navigation;
  if (state.music.active) return TftViewMode::Music;
  return TftViewMode::Home;
}

TftViewMode TftRenderer::hitTestHomeApp(int16_t x, int16_t y) const {
  if (y >= 55 && y <= 117) return x < 160 ? TftViewMode::Navigation : TftViewMode::Music;
  if (y >= 128 && y <= 190) return x < 160 ? TftViewMode::Weather : TftViewMode::AutoStatus;
  return TftViewMode::Home;
}

MediaControlCommand TftRenderer::hitTestMediaControl(int16_t x, int16_t y) const {
  if (!musicControlsVisible || y < 195 || y > 232) {
    return MediaControlCommand::None;
  }
  if (x >= 15 && x <= 56) return MediaControlCommand::Previous;
  if (x >= 57 && x <= 98) return MediaControlCommand::PlayPause;
  if (x >= 99 && x <= 141) return MediaControlCommand::Next;
  return MediaControlCommand::None;
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

void TftRenderer::compositePhoneSheet(int16_t offsetY) {
  if (offsetY <= -AMAP_TFT_HEIGHT) return;
  uint16_t* base = canvas.pixels();
  const uint16_t* sheet = adjacentFrame.pixels();
  const int16_t firstSheetRow = max<int16_t>(0, -offsetY);
  const int16_t lastSheetRow = min<int16_t>(AMAP_TFT_HEIGHT, AMAP_TFT_HEIGHT - offsetY);
  for (int16_t sheetRow = firstSheetRow; sheetRow < lastSheetRow; ++sheetRow) {
    const int16_t destinationRow = sheetRow + offsetY;
    memcpy(base + destinationRow * AMAP_TFT_WIDTH, sheet + sheetRow * AMAP_TFT_WIDTH,
           AMAP_TFT_WIDTH * sizeof(uint16_t));
  }
}

void TftRenderer::compositeHomeTransition(int16_t offsetY) {
  if (offsetY >= AMAP_TFT_HEIGHT) return;
  uint16_t* base = canvas.pixels();
  const uint16_t* home = adjacentFrame.pixels();
  const uint16_t* app = homeTransitionFrame.pixels();
  memcpy(base, home, kPixelBytes);
  const float progress = constrain(static_cast<float>(AMAP_TFT_HEIGHT - offsetY) /
                                       AMAP_TFT_HEIGHT,
                                   0.0f, 1.0f);
  int16_t targetLeft = 22;
  int16_t targetTop = 66 - homeScroll;
  const char* iconApp = "map";
  uint16_t iconSurface = 0x03EC;
  if (viewMode == TftViewMode::Music) {
    targetLeft = 176;
    iconApp = "music";
    iconSurface = 0x715C;
  } else if (viewMode == TftViewMode::Weather) {
    targetTop = 139 - homeScroll;
    iconApp = "weather";
    iconSurface = 0xFC40;
  }
  else if (viewMode == TftViewMode::Settings) {
    targetLeft = settingsPage == 1 ? 176 : 22;
    targetTop = 212 - homeScroll;
    iconApp = settingsPage == 1 ? "display" : "settings";
    iconSurface = settingsPage == 1 ? 0x715C : 0x014A;
  } else if (viewMode == TftViewMode::Auto || viewMode == TftViewMode::AutoStatus) {
    targetLeft = 176;
    targetTop = 139 - homeScroll;
    iconApp = "auto";
    iconSurface = 0x04DF;
  }
  const int16_t width = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_WIDTH -
      progress * (AMAP_TFT_WIDTH - 38)));
  const int16_t height = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_HEIGHT -
      progress * (AMAP_TFT_HEIGHT - 38)));
  const int16_t left = static_cast<int16_t>(progress * targetLeft);
  const int16_t top = static_cast<int16_t>(progress * targetTop);
  for (int16_t y = 0; y < height && top + y < AMAP_TFT_HEIGHT; ++y) {
    const int16_t sourceY = y * AMAP_TFT_HEIGHT / height;
    for (int16_t x = 0; x < width && left + x < AMAP_TFT_WIDTH; ++x) {
      const int16_t sourceX = x * AMAP_TFT_WIDTH / width;
      base[(top + y) * AMAP_TFT_WIDTH + left + x] =
          app[sourceY * AMAP_TFT_WIDTH + sourceX];
    }
  }
  // The source app first shrinks toward its launcher tile, then the tile's
  // own icon grows over it until the final frame is exactly that app icon.
  const float iconProgress = constrain((progress - 0.55f) / 0.45f, 0.0f, 1.0f);
  if (iconProgress > 0.0f) {
    const int16_t iconSize = static_cast<int16_t>(8 + iconProgress * 30.0f);
    const int16_t iconLeft = targetLeft + (38 - iconSize) / 2;
    const int16_t iconTop = targetTop + (38 - iconSize) / 2;
    TftFrameRenderer::drawAppIcon(canvas, iconLeft, iconTop, iconSize, iconApp, iconSurface);
  }
}

void TftRenderer::render(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                         const WeatherState& weather) {
  if (!ready) {
    return;
  }
  const bool connected = wifiConnected || bleConnected;
  const unsigned long now = millis();
  advanceSpring(now);
  advancePhoneSheetSpring(now);
  advanceHomeTransitionSpring(now);
  if (!touching && musicLyricReturnAt != 0 && now >= musicLyricReturnAt &&
      musicLyricOffsetY != 0) {
    const unsigned long elapsed = min<unsigned long>(now - lastMusicLyricFrameAt, 34UL);
    lastMusicLyricFrameAt = now;
    const int16_t step = max<int16_t>(1, static_cast<int16_t>(elapsed / 7UL));
    if (abs(musicLyricOffsetY) <= step) musicLyricOffsetY = 0;
    else musicLyricOffsetY += musicLyricOffsetY > 0 ? -step : step;
    frameDrawn = false;
  }
  const TftViewMode renderedView = viewMode == TftViewMode::Auto && displayPreferences.autoView
      ? resolveAutoView(state, connected, silenceMs)
      : (viewMode == TftViewMode::Auto ? TftViewMode::Home : viewMode);
  const bool automaticMode = displayPreferences.autoView;
  const int8_t renderedPressedRow = viewMode == TftViewMode::AutoStatus && autoStatusPressed
      ? 0 : settingsRow;
  musicControlsVisible = state.music.active && connected &&
      ((renderedView == TftViewMode::Music && silenceMs <= AMAP_STANDBY_MS) ||
       (renderedView == TftViewMode::Auto && silenceMs <= AMAP_STALE_MS && !state.active));
  const bool showGestureHint = pressedMediaControl == MediaControlCommand::None &&
                               (touching || springActive || phoneSheetSpringActive ||
                                homeTransitionSpringActive ||
                                static_cast<long>(gestureHintUntil - now) > 0);
  const uint32_t signature =
      frameSignature(state, wifiConnected, bleConnected, ip, port, silenceMs, now,
                       renderedView, dragOffsetX, showGestureHint, pressedMediaControl, renderedPressedRow,
                      phoneDetail, phoneDetailScroll, phoneSheetVisible, phoneSheetOffsetY,
                       automaticMode, homeTransitionVisible, homeTransitionOffsetY, settingsPage,
                       homeScroll, musicLyricOffsetY, weather);
  if (frameDrawn && signature == lastFrameSignature) {
    return;
  }

  const uint32_t frameStartedAt = micros();
  TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip, port,
                             silenceMs, weather, renderedView, pressedMediaControl, renderedPressedRow, phoneDetail,
                             phoneDetailScroll, automaticMode, settingsPage, homeScroll,
                             musicLyricOffsetY);
  if (homeTransitionVisible) {
    TftFrameRenderer::render(adjacentFrame, adjacentFont, state, wifiConnected, bleConnected,
                              ip, port, silenceMs, weather, TftViewMode::Home,
                             MediaControlCommand::None, -1, false, 0, false, 0, homeScroll);
    compositeHomeTransition(homeTransitionOffsetY);
  } else if (phoneSheetVisible) {
    TftFrameRenderer::renderPhoneSheet(adjacentFrame, adjacentFont, state.phone,
                                       wifiConnected, bleConnected, phoneDetail,
                                       phoneDetailScroll);
    compositePhoneSheet(phoneSheetOffsetY);
  }
  if (showGestureHint) {
    TftFrameRenderer::drawGestureHint(canvas, tftFont, renderedView);
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

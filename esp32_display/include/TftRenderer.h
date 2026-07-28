#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config.h"
#include "NavState.h"
#include "MediaControlCommand.h"
#include "TftViewMode.h"
#include "DisplayPreferences.h"
#include "WeatherService.h"

struct TftRenderRegions;

// Buffered SPI TFT dashboard. A complete frame is composed in PSRAM before it
// is sent to the ST7789V or ILI9341V panel, so users never see the
// clear-and-redraw process.
class TftRenderer {
public:
  ~TftRenderer();
  void begin();
  bool isReady() const;
  void updateTouch(uint8_t touchCount, int16_t x = 0, int16_t y = 0);
  bool takeMediaControlCommand(MediaControlCommand& command);
  bool takeWeatherRetryRequest();
  void render(const NavState& state, bool wifiConnected, bool bleConnected, const String& ip,
              uint16_t port, unsigned long silenceMs, const WeatherState& weather);
  const char* currentViewName() const;

private:
  class Canvas : public Adafruit_GFX {
  public:
    Canvas();
    ~Canvas();

    bool begin();
    uint16_t* pixels();
    void swapBuffer(Canvas& other);
    void setRenderRegions(const TftRenderRegions* regions);
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override;
    void fillScreen(uint16_t color) override;

  private:
    uint16_t* buffer = nullptr;
    const TftRenderRegions* renderRegions = nullptr;
  };

  Canvas canvas;
  Canvas previousFrame;
  Canvas adjacentFrame;
  Canvas homeTransitionFrame;
  Canvas transferFrame;
  Canvas pageCache;
  uint16_t* transferBuffer = nullptr;
  void pushRectangle(int16_t x, int16_t y, int16_t width, int16_t height,
                     const uint16_t* source, int16_t sourceStride);
  void writeRectangle(int16_t x, int16_t y, int16_t width, int16_t height,
                      const uint16_t* source, int16_t sourceStride);
  bool ready = false;
  bool frameDrawn = false;
  bool previousFrameValid = false;
  uint32_t lastFrameSignature = 0;
  SemaphoreHandle_t frameTransferDone = nullptr;
  TaskHandle_t frameTransferTask = nullptr;
  volatile uint32_t lastTransferDurationUs = 0;
  unsigned long lastPerformanceLogAt = 0;
  static constexpr uint8_t PAGE_COMPONENT_COUNT = 8;
  bool pageCacheValid = false;
  TftViewMode pageCacheView = TftViewMode::Auto;
  uint32_t pageComponentSignatures[PAGE_COMPONENT_COUNT] = {};
  int8_t pageCachePressedSettingsRow = -1;
  bool lastShowGestureHint = false;
  int16_t lastHomeTransitionLeft = 0;
  int16_t lastHomeTransitionTop = 0;
  int16_t lastHomeTransitionWidth = 0;
  int16_t lastHomeTransitionHeight = 0;
  bool lastHomeTransitionBoundsValid = false;
  int16_t lastPhoneSheetOffsetY = -AMAP_TFT_HEIGHT;
  bool lastPhoneSheetOffsetValid = false;
  // Boot to a predictable app launcher; automatic switching never steals the
  // foreground from an explicitly opened app.
  TftViewMode viewMode = TftViewMode::Home;
  bool touching = false;
  bool musicControlsVisible = false;
  MediaControlCommand pressedMediaControl = MediaControlCommand::None;
  MediaControlCommand pendingMediaControl = MediaControlCommand::None;
  bool directionLocked = false;
  bool horizontalGesture = false;
  bool horizontalBlocked = false;
  bool phoneSheetGesture = false;
  bool phoneSheetAnimationLocked = false;
  bool homeGesture = false;
  bool homeScrollGesture = false;
  bool phoneSheetVisible = false;
  bool phoneSheetBaseSnapshotReady = false;
  bool phoneSheetSnapshotReady = false;
  uint32_t phoneSheetContentSignature = 0;
  bool homeTransitionVisible = false;
  bool homeTransitionSnapshotReady = false;
  bool homeTransitionDestinationReady = false;
  int16_t touchStartX = 0;
  int16_t touchStartY = 0;
  int16_t touchBaseOffset = 0;
  int16_t homeScrollStart = 0;
  int16_t lastSampleX = 0;
  int16_t lastSampleY = 0;
  int16_t dragOffsetX = 0;
  int16_t phoneSheetOffsetY = -AMAP_TFT_HEIGHT;
  int16_t homeTransitionOffsetY = AMAP_TFT_HEIGHT;
  float releaseVelocityX = 0.0f;
  float releaseVelocityY = 0.0f;
  float springVelocity = 0.0f;
  int16_t springTarget = 0;
  bool springActive = false;
  float phoneSheetVelocity = 0.0f;
  int16_t phoneSheetTargetY = -AMAP_TFT_HEIGHT;
  bool phoneSheetSpringActive = false;
  float homeTransitionVelocity = 0.0f;
  int16_t homeTransitionTargetY = AMAP_TFT_HEIGHT;
  bool homeTransitionSpringActive = false;
  unsigned long touchStartedAt = 0;
  unsigned long lastSampleAt = 0;
  unsigned long lastSpringAt = 0;
  unsigned long gestureHintUntil = 0;
  unsigned long fpsWindowStartedAt = 0;
  uint16_t fpsFrameCount = 0;
  uint16_t framesPerSecond = 0;
  DisplayPreferences displayPreferences;
  int8_t settingsRow = -1;
  uint8_t settingsPage = 0;
  int16_t homeScroll = 0;
  bool autoStatusPressed = false;
  bool weatherRetryAvailable = false;
  bool weatherRetryPressed = false;
  bool pendingWeatherRetry = false;
  bool phoneDetail = false;
  uint8_t phoneDetailScroll = 0;

  void beginTouch(int16_t x, int16_t y, unsigned long now);
  void moveTouch(int16_t x, int16_t y, unsigned long now);
  void endTouch(unsigned long now);
  void advanceSpring(unsigned long now);
  void advancePhoneSheetSpring(unsigned long now);
  void advanceHomeTransitionSpring(unsigned long now);
  void finishHomeTransition(bool commitHome, unsigned long now);
  void switchView(TftViewMode mode, unsigned long now);
  void finishHorizontalTransition();
  void compositeHorizontalSlide(int16_t offset);
  TftViewMode adjacentView(int direction) const;
  TftViewMode resolveAutoView(const NavState& state, bool connected,
                              unsigned long silenceMs) const;
  MediaControlCommand hitTestMediaControl(int16_t x, int16_t y) const;
  bool hitTestWeatherRetry(int16_t x, int16_t y) const;
  TftViewMode hitTestHomeApp(int16_t x, int16_t y) const;
  void applyBrightness();
  void cycleSettingsRow();
  void recordRenderedFrame(unsigned long now);
  static void frameTransferTaskEntry(void* context);
  void waitForFrameTransfer();
  void queueFullFrameTransfer();
  void compositePhoneSheet(int16_t offsetY);
  void compositeHomeTransition(int16_t offsetY);
};

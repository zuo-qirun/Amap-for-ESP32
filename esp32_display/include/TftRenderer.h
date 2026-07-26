#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include "Config.h"
#include "NavState.h"
#include "MediaControlCommand.h"
#include "TftViewMode.h"
#include "DisplayPreferences.h"
#include "WeatherService.h"

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
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override;
    void fillScreen(uint16_t color) override;

  private:
    uint16_t* buffer = nullptr;
  };

  Canvas canvas;
  Canvas previousFrame;
  Canvas adjacentFrame;
  Canvas homeTransitionFrame;
  uint16_t* transferBuffer = nullptr;
  void pushRectangle(int16_t x, int16_t y, int16_t width, int16_t height,
                     const uint16_t* source, int16_t sourceStride);
  bool ready = false;
  bool frameDrawn = false;
  uint32_t lastFrameSignature = 0;
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
  bool musicLyricGesture = false;
  bool phoneSheetVisible = false;
  bool homeTransitionVisible = false;
  bool homeTransitionSnapshotReady = false;
  int16_t touchStartX = 0;
  int16_t touchStartY = 0;
  int16_t touchBaseOffset = 0;
  int16_t homeScrollStart = 0;
  int16_t musicLyricOffsetStart = 0;
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
  DisplayPreferences displayPreferences;
  int8_t settingsRow = -1;
  uint8_t settingsPage = 0;
  int16_t homeScroll = 0;
  int16_t musicLyricOffsetY = 0;
  unsigned long musicLyricReturnAt = 0;
  unsigned long lastMusicLyricFrameAt = 0;
  bool autoStatusPressed = false;
  bool phoneDetail = false;
  uint8_t phoneDetailScroll = 0;

  void beginTouch(int16_t x, int16_t y, unsigned long now);
  void moveTouch(int16_t x, int16_t y, unsigned long now);
  void endTouch(unsigned long now);
  void advanceSpring(unsigned long now);
  void advancePhoneSheetSpring(unsigned long now);
  void advanceHomeTransitionSpring(unsigned long now);
  void switchView(TftViewMode mode, unsigned long now);
  void finishHorizontalTransition();
  void compositeHorizontalSlide(int16_t offset);
  TftViewMode adjacentView(int direction) const;
  TftViewMode resolveAutoView(const NavState& state, bool connected,
                              unsigned long silenceMs) const;
  MediaControlCommand hitTestMediaControl(int16_t x, int16_t y) const;
  TftViewMode hitTestHomeApp(int16_t x, int16_t y) const;
  void applyBrightness();
  void cycleSettingsRow();
  void compositePhoneSheet(int16_t offsetY);
  void compositeHomeTransition(int16_t offsetY);
};

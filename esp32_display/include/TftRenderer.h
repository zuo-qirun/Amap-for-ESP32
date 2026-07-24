#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include "NavState.h"
#include "TftViewMode.h"

// Buffered SPI TFT dashboard. A complete frame is composed in PSRAM before it
// is sent to the ST7789V or ILI9341V panel, so users never see the
// clear-and-redraw process.
class TftRenderer {
public:
  ~TftRenderer();
  void begin();
  bool isReady() const;
  void updateTouch(uint8_t touchCount, int16_t x = 0, int16_t y = 0);
  void render(const NavState& state, bool wifiConnected, bool bleConnected, const String& ip,
              uint16_t port, unsigned long silenceMs);
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
  uint16_t* transferBuffer = nullptr;
  void pushRectangle(int16_t x, int16_t y, int16_t width, int16_t height,
                     const uint16_t* source, int16_t sourceStride);
  bool ready = false;
  bool frameDrawn = false;
  uint32_t lastFrameSignature = 0;
  TftViewMode viewMode = TftViewMode::Auto;
  bool touching = false;
  bool directionLocked = false;
  bool horizontalGesture = false;
  int16_t touchStartX = 0;
  int16_t touchStartY = 0;
  int16_t touchBaseOffset = 0;
  int16_t lastSampleX = 0;
  int16_t lastSampleY = 0;
  int16_t dragOffsetX = 0;
  float releaseVelocityX = 0.0f;
  float springVelocity = 0.0f;
  int16_t springTarget = 0;
  bool springActive = false;
  unsigned long touchStartedAt = 0;
  unsigned long lastSampleAt = 0;
  unsigned long lastSpringAt = 0;
  unsigned long gestureHintUntil = 0;

  void beginTouch(int16_t x, int16_t y, unsigned long now);
  void moveTouch(int16_t x, int16_t y, unsigned long now);
  void endTouch(unsigned long now);
  void advanceSpring(unsigned long now);
  void switchView(TftViewMode mode, unsigned long now);
  void finishHorizontalTransition();
  void compositeHorizontalSlide(int16_t offset);
  TftViewMode adjacentView(int direction) const;
};

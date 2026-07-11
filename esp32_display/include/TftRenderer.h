#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include "NavState.h"

// Buffered ST7789 dashboard. A complete frame is composed in PSRAM before it
// is sent to the panel, so users never see the clear-and-redraw process.
class TftRenderer {
public:
  void begin();
  bool isReady() const;
  void render(const NavState& state, bool wifiConnected, bool bleConnected, const String& ip,
              uint16_t port, unsigned long silenceMs);

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
  bool ready = false;
  bool frameDrawn = false;
  uint32_t lastFrameSignature = 0;
};

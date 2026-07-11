#pragma once

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WebServer.h>

#include "NavState.h"

class TftPreviewRenderer {
public:
  bool sendBmp(WebServer& server, const NavState& state, bool wifiConnected,
               unsigned long silenceMs);

private:
  class Canvas : public Adafruit_GFX {
  public:
    Canvas();
    ~Canvas();

    bool begin();
    bool isReady() const;
    const uint16_t* pixels() const;

    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override;
    void fillScreen(uint16_t color) override;

  private:
    uint16_t* buffer = nullptr;
  };

  Canvas canvas;
  U8G2_FOR_ADAFRUIT_GFX font;
  bool fontReady = false;
};

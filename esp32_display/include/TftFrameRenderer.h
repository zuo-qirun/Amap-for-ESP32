#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "NavState.h"

// Draws the logical ST7789 frame onto any Adafruit_GFX-compatible target.
// The hardware display and browser preview deliberately share this renderer.
class TftFrameRenderer {
public:
  static void render(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                     const NavState& state, bool wifiConnected, bool bleConnected,
                     const String& ip, uint16_t port, unsigned long silenceMs);

private:
  static void renderStandby(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                            const String& title, const String& detail, bool wifiConnected,
                            bool bleConnected, const String& ip, uint16_t port);
  static void renderNavigation(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const NavState& state);
  static void renderCruise(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                           const NavState& state);
  static void drawShell(Adafruit_GFX& display);
  static void drawNavigationInfo(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                 const NavState& state, int16_t top);
  static void drawCruiseInfo(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                             const NavState& state, int16_t top);
  static void drawUtf8(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                       const String& text, uint16_t color);
  static void drawClipped(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                          int16_t maxWidth, const String& text, uint16_t color);
  static void drawBig(Adafruit_GFX& display, int16_t x, int16_t top, const String& text,
                      uint8_t scale, uint16_t color);
  static void drawTurnIcon(Adafruit_GFX& display, int icon, int16_t x, int16_t y,
                           uint16_t color, uint16_t background);
  static void drawCameraIcon(Adafruit_GFX& display, int type, int16_t x, int16_t y,
                             uint16_t color, uint16_t background);
  static void drawCameraPill(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                             const NavState& state, int16_t x, int16_t y, int16_t width);
  static void drawNavigationTrafficPill(Adafruit_GFX& display, const NavState& state);
  static void drawCruiseTrafficPills(Adafruit_GFX& display, const NavState& state,
                                     int16_t top);
  static int16_t drawTrafficPill(Adafruit_GFX& display, const LightState& light, int16_t left,
                                 int16_t top, bool compact);
  static void drawLanes(Adafruit_GFX& display, const NavState& state, int16_t top);
  static void drawLightDirection(Adafruit_GFX& display, int dir, int16_t cx, int16_t cy,
                                 uint16_t color, uint16_t background);
  static void drawTmc(Adafruit_GFX& display, const NavState& state, int16_t x, int16_t y,
                      int16_t width);
  static String formatCamera(const NavState& state);
};

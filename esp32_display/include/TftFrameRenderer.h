#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "NavState.h"
#include "MediaControlCommand.h"
#include "TftViewMode.h"
#include "DisplayPreferences.h"
#include "WeatherService.h"

struct TftRenderRect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;
};

// A small explicit region list lets the renderer restore and repaint only the
// components whose visible dependencies changed. Coordinates remain in the
// existing 320x240 design space; no layout or motion values are transformed.
struct TftRenderRegions {
  static constexpr uint8_t MAX_RECTS = 10;
  TftRenderRect rects[MAX_RECTS];
  uint8_t count = 0;

  void clear() { count = 0; }
  void add(int16_t x, int16_t y, int16_t width, int16_t height);
  bool contains(int16_t x, int16_t y) const;
  bool intersects(int16_t x, int16_t y, int16_t width, int16_t height) const;
  uint32_t pixelCount() const;
};

// Draws the logical 320x240 frame onto any Adafruit_GFX-compatible target.
// The hardware display and browser preview deliberately share this renderer.
class TftFrameRenderer {
public:
  static void render(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                     const NavState& state, bool wifiConnected, bool bleConnected,
                     const String& ip, uint16_t port, unsigned long silenceMs,
                     const WeatherState& weather,
                     TftViewMode viewMode = TftViewMode::Auto,
                     MediaControlCommand pressedControl = MediaControlCommand::None,
                     int8_t pressedSettingsRow = -1, bool phoneDetail = false,
                     uint8_t phoneDetailScroll = 0, bool autoMode = false,
                     uint8_t settingsPage = 0, int16_t homeScroll = 0,
                     bool weatherRetryPressed = false,
                     MusicPageStyle musicPageStyle = MusicPageStyle::Standard,
                     const DisplayPreferences* preferences = nullptr,
                     const TftRenderRegions* regions = nullptr);
  // The phone information surface is composed independently by TftRenderer so
  // it can physically follow a downward swipe over whichever app is open.
  static void renderPhoneSheet(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const PhoneState& phone, bool wifiConnected,
                               bool bleConnected, bool detail, uint8_t detailScroll,
                               const TftRenderRegions* regions = nullptr);
  static void drawGestureHint(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                              TftViewMode viewMode);
  static void drawFrameRate(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                            uint16_t framesPerSecond);
  static void drawAppIcon(Adafruit_GFX& display, int16_t left, int16_t top,
                          int16_t size, const String& app, uint16_t surface);

private:
  static void renderStandby(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                            const String& title, const String& detail, bool wifiConnected,
                            bool bleConnected, const String& ip, uint16_t port);
  static void renderNavigation(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const NavState& state, const TftRenderRegions* regions);
  static void renderCruise(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                           const NavState& state, const TftRenderRegions* regions);
  static void renderMusic(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                          const MusicState& music, MediaControlCommand pressedControl,
                          MusicPageStyle pageStyle, const TftRenderRegions* regions);
  static void renderMusicPipWindow(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                   const MusicState& music, const TftRenderRegions* regions);
  static void renderMusicRefinedNowPlaying(Adafruit_GFX& display,
                                           U8G2_FOR_ADAFRUIT_GFX& font,
                                           const MusicState& music,
                                           MediaControlCommand pressedControl,
                                           const TftRenderRegions* regions);
  static void renderHome(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                         const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, bool autoMode, int16_t homeScroll,
                         const WeatherState& weather, const TftRenderRegions* regions);
  static void renderWeather(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                            const WeatherState& weather, bool wifiConnected,
                            bool retryPressed, const TftRenderRegions* regions);
  static void renderAutoStatus(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const NavState& state, bool wifiConnected, bool bleConnected,
                               const String& ip, uint16_t port, bool autoMode,
                               bool pressed, const TftRenderRegions* regions);
  static void renderSettings(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                             bool wifiConnected, bool bleConnected, const String& ip,
                             uint16_t port, int8_t pressedRow, uint8_t settingsPage,
                             const DisplayPreferences& settings,
                             const TftRenderRegions* regions);
  static void drawPhoneOverlay(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const PhoneState& phone, bool messageBanners);
  static void drawMusicOverlay(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                               const MusicState& music);
  static void drawShell(Adafruit_GFX& display);
  static void drawNavigationInfo(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                 const NavState& state, int16_t top);
  static void drawCruiseInfo(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                             const NavState& state, int16_t top);
  static void drawUtf8(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                       const String& text, uint16_t color);
  static void drawClipped(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                          int16_t maxWidth, const String& text, uint16_t color);
  static void drawKaraokeLine(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                              int16_t maxWidth, const String& text,
                              const String& highlighted, const String& currentWord,
                              int wordProgressPermille, uint16_t idleColor,
                              uint16_t activeColor);
  static void drawTimedScrollingLine(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x,
                                     int16_t baseline, int16_t maxWidth,
                                     const String& text, int64_t positionMs,
                                     int64_t lineStartMs, int64_t lineDurationMs,
                                     uint16_t color);
  static void drawBig(Adafruit_GFX& display, int16_t x, int16_t top, const String& text,
                      uint8_t scale, uint16_t color);
  static void drawTurnIcon(Adafruit_GFX& display, int icon, int16_t x, int16_t y,
                           uint16_t color, uint16_t background);
  static void drawCameraIcon(Adafruit_GFX& display, int type, int16_t x, int16_t y,
                             uint16_t color, uint16_t background);
  static void drawCameraPill(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                             const NavState& state, int16_t x, int16_t y, int16_t width);
  static void drawSpeedLimitSign(Adafruit_GFX& display, const NavState& state,
                                 int16_t centerX, int16_t centerY);
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
  static String formatTime(int64_t milliseconds);
};

#pragma once

#include <Arduino.h>
#include "NavState.h"

class DisplayRenderer {
public:
  void begin();
  void render(const NavState& state, bool wifiConnected, bool bleConnected, const String& ip,
              uint16_t port, unsigned long silenceMs);

private:
  void renderNetwork(bool wifiConnected, const String& ip, uint16_t port);
  void renderStandby(const String& message, bool wifiConnected, bool bleConnected,
                     const String& ip, uint16_t port);
  void renderNav(const NavState& state, unsigned long silenceMs);
  void renderMusic(const MusicState& music);
  void setTextFont();
  void setSmallFont();
  void setLargeFont();
  void drawClipped(int x, int y, int maxWidth, const String& text);
  void drawTurnIcon(int icon, int x, int y);
  void drawCameraIcon(int type, int x, int y);
  String clipped(String text, int maxWidth);
  String modeLabel(const String& mode) const;
  String laneText(const NavState& state) const;
  String lightText(const NavState& state) const;
  String cameraText(const NavState& state) const;
  String tmcText(const NavState& state) const;
  String routeText(const NavState& state) const;
  String guideText(const NavState& state) const;
  String roadInfoText(const NavState& state) const;
  String bottomText(const NavState& state) const;
  String formatTime(int64_t milliseconds) const;
};

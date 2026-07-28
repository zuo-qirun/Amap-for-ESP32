#pragma once

#include <Arduino.h>

enum class MusicPageStyle : uint8_t {
  Standard = 0,
  PipWindow = 1,
  RefinedNowPlaying = 2,
};

struct DisplayPreferences {
  uint8_t brightness = 100;
  bool nightDim = false;
  bool autoView = true;
  bool messageBanners = true;
  // Standard keeps the firmware-native layout, PiPWindow is the compact card,
  // and RefinedNowPlaying mirrors solstice23's immersive BetterNCM page.
  MusicPageStyle musicPageStyle = MusicPageStyle::Standard;
  bool showFrameRate = false;

  static DisplayPreferences load();
  bool save() const;
  uint8_t effectiveBrightness() const;
};

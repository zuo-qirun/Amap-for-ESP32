#pragma once

#include <Arduino.h>

struct DisplayPreferences {
  uint8_t brightness = 100;
  bool nightDim = false;
  bool autoView = true;
  bool messageBanners = true;

  static DisplayPreferences load();
  bool save() const;
  uint8_t effectiveBrightness() const;
};

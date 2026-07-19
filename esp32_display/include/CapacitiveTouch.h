#pragma once

#include <Arduino.h>

struct CapacitiveTouchPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// FT6336U input for the capacitive Hongxun 28005 module. Coordinates are
// transformed to the same 320x240 landscape space used by TftRenderer.
class CapacitiveTouch {
public:
  void begin();
  void update();
  bool isReady() const;
  uint8_t touchCount() const;
  CapacitiveTouchPoint point(uint8_t index = 0) const;

private:
  CapacitiveTouchPoint transform(int16_t rawX, int16_t rawY) const;

  CapacitiveTouchPoint points[2];
  uint8_t count = 0;
  bool ready = false;
  unsigned long lastPollAt = 0;
};

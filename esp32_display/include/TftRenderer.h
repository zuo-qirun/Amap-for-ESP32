#pragma once

#include <Arduino.h>
#include "NavState.h"

// Direct-mode ST7789 dashboard. Its drawing commands are shared with the
// browser bitmap preview through TftFrameRenderer.
class TftRenderer {
public:
  void begin();
  bool isReady() const;
  void render(const NavState& state, bool wifiConnected, const String& ip,
              uint16_t port, unsigned long silenceMs);

private:
  bool ready = false;
};

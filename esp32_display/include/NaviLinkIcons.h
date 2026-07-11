#pragma once

#include <Arduino.h>

namespace NaviLinkIcons {
struct Bitmap {
  const uint8_t* alpha;
  uint8_t width;
  uint8_t height;
};

const Bitmap& turnBitmap(int icon);
const Bitmap& trafficDirectionBitmap(int direction);
const Bitmap& cameraBitmap(int type);
const Bitmap* laneBitmap(int code);
}  // namespace NaviLinkIcons

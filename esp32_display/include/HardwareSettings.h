#pragma once

#include <Arduino.h>

#include "Config.h"

struct HardwareSettings {
  int tftDriver = AMAP_TFT_DRIVER;
  bool touchEnabled = AMAP_TFT_TOUCH_DRIVER == AMAP_TFT_TOUCH_DRIVER_FT6336U;

  static HardwareSettings load();
  bool save() const;
  bool isValid() const;
  const char* tftDriverName() const;
};

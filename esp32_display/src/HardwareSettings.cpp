#include "HardwareSettings.h"

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "amap_hw";
constexpr const char* kTftDriver = "tft_driver";
constexpr const char* kTouchEnabled = "touch";
}  // namespace

HardwareSettings HardwareSettings::load() {
  HardwareSettings settings;
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    settings.tftDriver = prefs.getInt(kTftDriver, AMAP_TFT_DRIVER);
    settings.touchEnabled = prefs.getBool(
        kTouchEnabled, AMAP_TFT_TOUCH_DRIVER == AMAP_TFT_TOUCH_DRIVER_FT6336U);
    prefs.end();
  }
  if (!settings.isValid()) {
    settings.tftDriver = AMAP_TFT_DRIVER_ST7789;
  }
  return settings;
}

bool HardwareSettings::save() const {
  if (!isValid()) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  const bool driverSaved = prefs.putInt(kTftDriver, tftDriver) > 0;
  const bool touchSaved = prefs.putBool(kTouchEnabled, touchEnabled) > 0;
  prefs.end();
  return driverSaved && touchSaved;
}

bool HardwareSettings::isValid() const {
  return tftDriver == AMAP_TFT_DRIVER_ST7789 ||
         tftDriver == AMAP_TFT_DRIVER_ILI9341;
}

const char* HardwareSettings::tftDriverName() const {
  return tftDriver == AMAP_TFT_DRIVER_ILI9341 ? "ILI9341V" : "ST7789V";
}

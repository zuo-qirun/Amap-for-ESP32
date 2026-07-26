#include "DisplayPreferences.h"

#include <Preferences.h>

namespace { constexpr const char* kNamespace = "amap_display"; }

DisplayPreferences DisplayPreferences::load() {
  DisplayPreferences value;
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    value.brightness = constrain(prefs.getUChar("brightness", 100), 20, 100);
    value.nightDim = prefs.getBool("night", false);
    value.autoView = prefs.getBool("auto_view", true);
    value.messageBanners = prefs.getBool("banners", true);
    prefs.end();
  }
  return value;
}

bool DisplayPreferences::save() const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.putUChar("brightness", constrain(brightness, 20, 100)) > 0 &&
      prefs.putBool("night", nightDim) > 0 && prefs.putBool("auto_view", autoView) > 0 &&
      prefs.putBool("banners", messageBanners) > 0;
  prefs.end();
  return ok;
}

uint8_t DisplayPreferences::effectiveBrightness() const {
  return nightDim ? max<uint8_t>(20, brightness / 3) : brightness;
}

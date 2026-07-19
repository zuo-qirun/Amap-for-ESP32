#include "CapacitiveTouch.h"

#include <Adafruit_FT6206.h>
#include <Wire.h>

#include "Config.h"
#include "HardwareSettings.h"

namespace {
TwoWire touchWire(1);
Adafruit_FT6206 touchController;

constexpr unsigned long kPollIntervalMs = 10UL;
}  // namespace

void CapacitiveTouch::begin() {
  const HardwareSettings hardware = HardwareSettings::load();
  if (!hardware.touchEnabled) {
    Serial.println("Capacitive touch disabled by saved hardware settings");
    return;
  }
  if (AMAP_TFT_TOUCH_SDA_PIN < 0 || AMAP_TFT_TOUCH_SCL_PIN < 0) {
    Serial.println("FT6336U disabled: I2C pins are not configured");
    return;
  }

  if (AMAP_TFT_TOUCH_INT_PIN >= 0) {
    pinMode(AMAP_TFT_TOUCH_INT_PIN, INPUT_PULLUP);
  }
  if (AMAP_TFT_TOUCH_RST_PIN >= 0) {
    pinMode(AMAP_TFT_TOUCH_RST_PIN, OUTPUT);
    digitalWrite(AMAP_TFT_TOUCH_RST_PIN, LOW);
    delay(6);
    digitalWrite(AMAP_TFT_TOUCH_RST_PIN, HIGH);
  }
  // The FT6336U datasheet specifies up to 300 ms before points are reported
  // after power-on or reset.
  delay(300);

  if (!touchWire.begin(AMAP_TFT_TOUCH_SDA_PIN, AMAP_TFT_TOUCH_SCL_PIN,
                       AMAP_TFT_TOUCH_I2C_FREQUENCY)) {
    Serial.println("FT6336U disabled: unable to start I2C bus");
    return;
  }
  if (!touchController.begin(AMAP_TFT_TOUCH_THRESHOLD, &touchWire,
                             AMAP_TFT_TOUCH_I2C_ADDRESS)) {
    Serial.printf("FT6336U not found at I2C 0x%02X\n", AMAP_TFT_TOUCH_I2C_ADDRESS);
    return;
  }

  ready = true;
  Serial.printf("FT6336U ready: I2C SDA=%d SCL=%d RST=%d INT=%d address=0x%02X\n",
                AMAP_TFT_TOUCH_SDA_PIN, AMAP_TFT_TOUCH_SCL_PIN,
                AMAP_TFT_TOUCH_RST_PIN, AMAP_TFT_TOUCH_INT_PIN,
                AMAP_TFT_TOUCH_I2C_ADDRESS);
}

void CapacitiveTouch::update() {
  if (!ready || millis() - lastPollAt < kPollIntervalMs) {
    return;
  }
  lastPollAt = millis();

  const uint8_t reportedCount = touchController.touched();
  count = reportedCount > 2 ? 0 : reportedCount;
  for (uint8_t i = 0; i < count; ++i) {
    const TS_Point raw = touchController.getPoint(i);
    points[i] = transform(raw.x, raw.y);
  }
}

bool CapacitiveTouch::isReady() const {
  return ready;
}

uint8_t CapacitiveTouch::touchCount() const {
  return count;
}

CapacitiveTouchPoint CapacitiveTouch::point(uint8_t index) const {
  return index < count ? points[index] : CapacitiveTouchPoint{};
}

CapacitiveTouchPoint CapacitiveTouch::transform(int16_t rawX, int16_t rawY) const {
  int16_t x = rawX;
  int16_t y = rawY;
  int16_t xMax = AMAP_TFT_NATIVE_WIDTH - 1;
  int16_t yMax = AMAP_TFT_NATIVE_HEIGHT - 1;
#if AMAP_TFT_TOUCH_SWAP_XY
  const int16_t swapped = x;
  x = y;
  y = swapped;
  const int16_t swappedMax = xMax;
  xMax = yMax;
  yMax = swappedMax;
#endif
#if AMAP_TFT_TOUCH_INVERT_X
  x = xMax - x;
#endif
#if AMAP_TFT_TOUCH_INVERT_Y
  y = yMax - y;
#endif
  CapacitiveTouchPoint result;
  result.x = constrain(x, 0, AMAP_TFT_WIDTH - 1);
  result.y = constrain(y, 0, AMAP_TFT_HEIGHT - 1);
  return result;
}

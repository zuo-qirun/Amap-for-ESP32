#include "TftFrameRenderer.h"

#include <pgmspace.h>

#include "Config.h"
#include "NavigationIcons.h"
#include "NaviLinkIcons.h"

namespace {
constexpr uint16_t kCanvas = 0x0000;
constexpr uint16_t kSurface = 0x1082;       // Navi-Link #121212
constexpr uint16_t kInfoSurface = 0x2124;   // Navi-Link #242424
constexpr uint16_t kExitGreen = 0x03EC;     // Navi-Link #007D5E
constexpr uint16_t kCapsule = 0x014A;       // Navi-Link #002A50
constexpr uint16_t kCapsuleStroke = 0x4268;
constexpr uint16_t kAccent = 0x04DF;        // Navi-Link #0099FF
constexpr uint16_t kLaneBlue = 0x245C;      // Navi-Link #1E88E5
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kTextSoft = 0xE71C;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kDivider = 0x39E7;
constexpr uint16_t kRed = 0xF986;           // Navi-Link #FF3333
constexpr uint16_t kYellow = 0xCCC0;        // Navi-Link #CC9900
constexpr uint16_t kGreen = 0x366B;         // Navi-Link #34C759

uint16_t lightColor(int status) {
  return status == 1 ? kRed : (status == 4 ? kGreen : kYellow);
}

uint16_t tmcColor(int status) {
  switch (status) {
    case 0: return 0x2196;
    case 1: return 0x05E6;
    case 2: return 0xFFE0;
    case 3: return 0xF8A0;
    case 4: return 0xB800;
    case 5: return 0x03EF;
    default: return kMuted;
  }
}

String compact(String text, int maximum) {
  if (text.length() <= maximum) {
    return text;
  }
  text.remove(maximum);
  return text + "\xE2\x80\xA6";
}

String numericPart(const String& value) {
  String result;
  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if ((character >= '0' && character <= '9') || character == '.') {
      result += character;
    }
  }
  return result;
}

String unitPart(const String& value) {
  String result;
  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if (!((character >= '0' && character <= '9') || character == '.' || character == ' ')) {
      result += character;
    }
  }
  return result;
}

uint16_t alphaBlend(uint16_t background, uint16_t foreground, uint8_t alpha) {
  const uint16_t inverse = 255 - alpha;
  const uint16_t red = (((background >> 11) & 0x1F) * inverse +
                        ((foreground >> 11) & 0x1F) * alpha + 127) /
                       255;
  const uint16_t green = (((background >> 5) & 0x3F) * inverse +
                          ((foreground >> 5) & 0x3F) * alpha + 127) /
                         255;
  const uint16_t blue = ((background & 0x1F) * inverse + (foreground & 0x1F) * alpha + 127) /
                        255;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

void drawAlphaBitmap(Adafruit_GFX& display, const NaviLinkIcons::Bitmap& bitmap, int16_t x,
                     int16_t y, uint16_t color, uint16_t background) {
  for (uint8_t row = 0; row < bitmap.height; ++row) {
    for (uint8_t column = 0; column < bitmap.width; ++column) {
      const uint8_t alpha = pgm_read_byte(bitmap.alpha + row * bitmap.width + column);
      if (alpha != 0) {
        display.drawPixel(x + column, y + row, alphaBlend(background, color, alpha));
      }
    }
  }
}
}  // namespace

void TftFrameRenderer::render(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                              const NavState& state, bool wifiConnected,
                              unsigned long silenceMs) {
  if (!wifiConnected) {
    renderStandby(display, font, "AMap ESP32", "AP configuration mode");
  } else if (!state.active || silenceMs > AMAP_STANDBY_MS) {
    renderStandby(display, font, "AMap ESP32", "Waiting for navigation JSON");
  } else if (silenceMs > AMAP_STALE_MS) {
    renderStandby(display, font, "Navigation paused", "Waiting for phone data");
  } else if (state.mode == "cruise") {
    renderCruise(display, font, state);
  } else {
    renderNavigation(display, font, state);
  }
}

void TftFrameRenderer::drawShell(Adafruit_GFX& display) {
  display.fillScreen(kCanvas);
  display.fillRoundRect(3, 3, display.width() - 6, display.height() - 6, 12, kSurface);
}

void TftFrameRenderer::renderStandby(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                     const String& title, const String& detail) {
  drawShell(display);
  display.drawRoundRect(4, 4, display.width() - 8, display.height() - 8, 12, kCapsuleStroke);
  display.fillRoundRect(18, 58, display.width() - 36, 122, 12, kInfoSurface);
  display.fillRoundRect(34, 76, 82, 20, 10, kCapsule);
  drawUtf8(font, 49, 90, "AMAP", kAccent);
  drawUtf8(font, 34, 122, title, kText);
  drawClipped(font, 34, 148, display.width() - 68, detail, kTextSoft);
  drawUtf8(font, 34, 168, "NAVI-LINK STYLE / ST7789", kMuted);
}

void TftFrameRenderer::renderNavigation(Adafruit_GFX& display,
                                         U8G2_FOR_ADAFRUIT_GFX& font,
                                         const NavState& state) {
  drawShell(display);

  // layout_floating_navi_normal.xml: a 60dp exit tile within a 100dp header.
  display.fillRoundRect(10, 10, 60, 80, 8, kExitGreen);
  drawTurnIcon(display, state.turn.icon, 10, 20, kText, kExitGreen);
  const String distance = numericPart(state.turn.distanceText);
  const String unit = unitPart(state.turn.distanceText);
  drawBig(display, 80, 11, distance.isEmpty() ? "--" : distance, 4, kText);
  if (!unit.isEmpty()) {
    drawUtf8(font, 84 + distance.length() * 20, 47, unit, kText);
  }
  drawClipped(font, 80, 84, 134, state.turn.road.isEmpty() ? state.road : state.turn.road,
              kText);

  if (state.lightCount > 0) {
    drawNavigationTrafficPill(display, state);
  } else {
    drawCameraPill(display, font, state, 216, 5, 98);
  }

  int16_t nextTop = 100;
  if (state.lane.count > 0) {
    drawLanes(display, state, nextTop);
    nextTop += 48;
  }
  drawTmc(display, state, 10, nextTop + 2, display.width() - 20);
  drawNavigationInfo(display, font, state, nextTop + 18);
}

void TftFrameRenderer::renderCruise(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                    const NavState& state) {
  drawShell(display);

  // layout_floating_cruise_normal.xml: 40sp speed, road name, then optional
  // traffic-light row and the native lane-line strip.
  drawBig(display, 10, 10, state.speed.current >= 0 ? String(state.speed.current) : "--", 5,
          kAccent);
  display.fillRect(76, 14, 1, 32, alphaBlend(kSurface, kText, 0x33));
  drawClipped(font, 87, 39, 120, state.road, kText);

  int16_t laneTop = 60;
  if (state.lightCount > 0) {
    drawCruiseTrafficPills(display, state, 58);
    laneTop = 114;
  } else {
    drawCameraPill(display, font, state, 216, 8, 94);
  }
  drawLanes(display, state, laneTop);
}

void TftFrameRenderer::drawNavigationInfo(Adafruit_GFX& display,
                                           U8G2_FOR_ADAFRUIT_GFX& font,
                                           const NavState& state, int16_t top) {
  const int16_t height = display.height() - top - 6;
  display.fillRoundRect(7, top, display.width() - 14, height, 10, kInfoSurface);
  drawUtf8(font, 18, top + 18, "CURRENT", kAccent);
  drawClipped(font, 82, top + 18, display.width() - 102, state.road, kText);
  display.fillRect(18, top + 29, display.width() - 36, 1, kDivider);

  const String eta = state.eta.remainTimeText.isEmpty() ? "--" : state.eta.remainTimeText;
  const String distance = state.eta.remainDistanceText.isEmpty() ? "--" : state.eta.remainDistanceText;
  drawUtf8(font, 18, top + 49, eta, kTextSoft);
  display.fillRect(102, top + 37, 1, 18, kDivider);
  drawUtf8(font, 116, top + 49, distance, kTextSoft);

  String destination = state.route.destination;
  if (destination.isEmpty()) {
    destination = state.guide.exitName;
  }
  if (!destination.isEmpty()) {
    drawUtf8(font, 18, top + height - 10, "DEST", kMuted);
    drawClipped(font, 56, top + height - 10, display.width() - 72, destination, kMuted);
  } else if (state.speed.limit > 0) {
    drawUtf8(font, 18, top + height - 10, "LIMIT", kMuted);
    drawUtf8(font, 68, top + height - 10, String(state.speed.limit) + " km/h", kTextSoft);
  }
}

void TftFrameRenderer::drawCruiseInfo(Adafruit_GFX& display,
                                      U8G2_FOR_ADAFRUIT_GFX& font,
                                      const NavState& state, int16_t top) {
  const int16_t height = display.height() - top - 6;
  display.fillRoundRect(7, top, display.width() - 14, height, 10, kInfoSurface);
  drawUtf8(font, 18, top + 18, "CURRENT", kAccent);
  drawClipped(font, 82, top + 18, display.width() - 102, state.road, kText);
  display.fillRect(18, top + 28, display.width() - 36, 1, kDivider);

  const String eta = state.eta.remainTimeText.isEmpty() ? "--" : state.eta.remainTimeText;
  const String distance = state.eta.remainDistanceText.isEmpty() ? "--" : state.eta.remainDistanceText;
  drawUtf8(font, 18, top + 50, eta, kTextSoft);
  display.fillRect(102, top + 37, 1, 16, kDivider);
  drawUtf8(font, 116, top + 50, distance, kTextSoft);
  if (state.speed.limit > 0) {
    drawClipped(font, 226, top + 50, 72, "LIMIT " + String(state.speed.limit), kMuted);
  }
}

void TftFrameRenderer::drawUtf8(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                                const String& text, uint16_t color) {
  font.setForegroundColor(color);
  font.setCursor(x, baseline);
  font.print(text);
}

void TftFrameRenderer::drawClipped(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                                   int16_t maxWidth, const String& text, uint16_t color) {
  drawUtf8(font, x, baseline, compact(text, max(1, maxWidth / 7)), color);
}

void TftFrameRenderer::drawBig(Adafruit_GFX& display, int16_t x, int16_t top,
                               const String& text, uint8_t scale, uint16_t color) {
  display.setTextWrap(false);
  display.setTextColor(color);
  display.setTextSize(scale);
  display.setCursor(x, top);
  display.print(text);
  display.setTextSize(1);
}

void TftFrameRenderer::drawTurnIcon(Adafruit_GFX& display, int icon, int16_t x, int16_t y,
                                    uint16_t color, uint16_t background) {
  drawAlphaBitmap(display, NaviLinkIcons::turnBitmap(icon), x, y, color, background);
}

void TftFrameRenderer::drawCameraIcon(Adafruit_GFX& display, int type, int16_t x, int16_t y,
                                      uint16_t color, uint16_t background) {
  drawAlphaBitmap(display, NaviLinkIcons::cameraBitmap(type), x, y, color, background);
}

void TftFrameRenderer::drawCameraPill(Adafruit_GFX& display,
                                      U8G2_FOR_ADAFRUIT_GFX& font,
                                      const NavState& state, int16_t x, int16_t y,
                                      int16_t width) {
  if (state.camera.distance < 0) {
    return;
  }
  display.fillRoundRect(x, y, width, 50, 25, kCapsule);
  display.drawRoundRect(x, y, width, 50, 25, alphaBlend(kCapsule, kText, 0x33));
  const int16_t cx = x + 23;
  const int16_t cy = y + 25;
  display.fillCircle(cx, cy, 20, 0xE800);  // camera_shape outer red ring
  display.fillCircle(cx, cy, 18, kText);
  const int limit = state.camera.speedLimit > 0 ? state.camera.speedLimit : state.speed.limit;
  if (limit > 0) {
    display.drawCircle(cx, cy, 17, 0xE800);
    drawBig(display, x + 10, y + 13, String(limit), 3, kCanvas);
  } else {
    drawCameraIcon(display, state.camera.type, x + 4, y + 6, kCanvas, kText);
  }
  drawClipped(font, x + 49, y + 30, width - 55, String(state.camera.distance) + "m", kText);
}

namespace {
int16_t trafficPillWidth(const LightState& light, bool compact) {
  const int16_t iconSize = compact ? 35 : 45;
  const int16_t textScale = compact ? 3 : 4;
  const int16_t textWidth = String(max(0, light.seconds)).length() * 6 * textScale;
  return (compact ? 2 : 3) + iconSize + 4 + textWidth + (compact ? 6 : 10);
}
}  // namespace

int16_t TftFrameRenderer::drawTrafficPill(Adafruit_GFX& display, const LightState& light,
                                           int16_t left, int16_t top, bool compact) {
  const int16_t iconSize = compact ? 35 : 45;
  const int16_t height = compact ? 40 : 50;
  const int16_t width = trafficPillWidth(light, compact);
  const int16_t iconLeft = left + (compact ? 2 : 3);
  const int16_t iconTop = top + (height - iconSize) / 2;
  const int16_t radius = iconSize / 2;
  const uint16_t fill = lightColor(light.status);

  display.fillRoundRect(left, top, width, height, height / 2, kCapsule);
  display.drawRoundRect(left, top, width, height, height / 2, alphaBlend(kCapsule, kText, 0x33));
  display.fillCircle(iconLeft + radius, iconTop + radius, radius, kCanvas);
  display.fillCircle(iconLeft + radius, iconTop + radius, radius - (compact ? 2 : 3), fill);
  drawLightDirection(display, light.dir, iconLeft + radius, iconTop + radius, kText, fill);
  drawBig(display, iconLeft + iconSize + 4, top + (compact ? 8 : 5),
          String(max(0, light.seconds)), compact ? 3 : 4, kText);
  return width;
}

void TftFrameRenderer::drawNavigationTrafficPill(Adafruit_GFX& display, const NavState& state) {
  if (state.lightCount == 0) {
    return;
  }
  const LightState& light = state.lights[0];
  drawTrafficPill(display, light, display.width() - trafficPillWidth(light, false) - 5, 5, false);
}

void TftFrameRenderer::drawCruiseTrafficPills(Adafruit_GFX& display, const NavState& state,
                                              int16_t top) {
  const uint8_t count = min<uint8_t>(state.lightCount, 4);
  if (count == 0) {
    return;
  }
  const bool compact = count >= 3;
  const int16_t gap = 5;
  int16_t total = gap * (count - 1);
  for (uint8_t i = 0; i < count; ++i) {
    total += trafficPillWidth(state.lights[i], compact);
  }
  int16_t left = max<int16_t>(5, (display.width() - total) / 2);
  for (uint8_t i = 0; i < count; ++i) {
    left += drawTrafficPill(display, state.lights[i], left, top, compact) + gap;
  }
}

void TftFrameRenderer::drawLanes(Adafruit_GFX& display, const NavState& state, int16_t top) {
  if (state.lane.count == 0) {
    return;
  }
  const uint8_t count = state.lane.count;
  const bool compact = count <= 3;
  const int16_t margin = 2;
  const int16_t dividerSpace = 1 + margin * 2;
  const int16_t width = compact ? 16 + count * (36 + margin * 2) +
                                      (count - 1) * dividerSpace
                                : display.width() - 20;
  const int16_t left = (display.width() - width) / 2;
  display.fillRoundRect(left, top, width, 44, 8, kLaneBlue);

  int16_t cursor = left + 8;
  const int16_t flexibleCell = compact ? 36 :
      (width - 16 - (count - 1) * dividerSpace) / count;
  for (uint8_t i = 0; i < count; ++i) {
    const NaviLinkIcons::Bitmap* bitmap = NaviLinkIcons::laneBitmap(state.lane.lanes[i]);
    if (bitmap != nullptr) {
      drawAlphaBitmap(display, *bitmap, cursor + (flexibleCell - bitmap->width) / 2, top + 4,
                      kText, kLaneBlue);
    } else {
      const int16_t center = cursor + flexibleCell / 2;
      display.drawFastVLine(center, top + 10, 22, kText);
      display.fillTriangle(center - 5, top + 15, center + 5, top + 15, center, top + 7, kText);
    }
    cursor += flexibleCell;
    if (i + 1 < count) {
      display.drawFastVLine(cursor + margin, top + 8, 28, alphaBlend(kLaneBlue, kText, 0x44));
      cursor += dividerSpace;
    }
  }
}

void TftFrameRenderer::drawLightDirection(Adafruit_GFX& display, int dir, int16_t cx,
                                          int16_t cy, uint16_t color, uint16_t background) {
  const NaviLinkIcons::Bitmap& bitmap = NaviLinkIcons::trafficDirectionBitmap(dir);
  drawAlphaBitmap(display, bitmap, cx - bitmap.width / 2, cy - bitmap.height / 2, color,
                  background);
}

void TftFrameRenderer::drawTmc(Adafruit_GFX& display, const NavState& state, int16_t x,
                               int16_t y, int16_t width) {
  display.fillRoundRect(x, y, width, 8, 4, kCapsule);
  display.drawRoundRect(x, y, width, 8, 4, kCapsuleStroke);
  if (state.tmc.count == 0 || state.tmc.totalDistance <= 0) {
    return;
  }
  int total = 0;
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    total += max(0, state.tmc.distance[i]);
  }
  if (total <= 0) {
    total = state.tmc.totalDistance;
  }
  int16_t cursor = x + 1;
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    const int16_t segment =
        max<int16_t>(1, (width - 2) * max(0, state.tmc.distance[i]) / total);
    display.fillRect(cursor, y + 1, segment, 6, tmcColor(state.tmc.status[i]));
    cursor += segment;
  }
  const int16_t marker =
      x + 1 + (width - 2) * constrain(state.tmc.finishDistance, 0, state.tmc.totalDistance) /
                  state.tmc.totalDistance;
  display.fillTriangle(marker - 4, y - 3, marker + 4, y - 3, marker, y + 1, kText);
}

String TftFrameRenderer::formatCamera(const NavState& state) {
  String result = "Camera";
  if (state.camera.distance >= 0) {
    result += " " + String(state.camera.distance) + "m";
  }
  const int limit = state.camera.speedLimit > 0 ? state.camera.speedLimit : state.speed.limit;
  if (limit > 0) {
    result += " " + String(limit);
  }
  return result;
}

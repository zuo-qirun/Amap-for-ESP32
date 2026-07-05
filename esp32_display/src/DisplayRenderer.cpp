#include "DisplayRenderer.h"

#include <U8g2lib.h>
#include <Wire.h>
#include "Config.h"

#if AMAP_OLED_DRIVER == AMAP_OLED_DRIVER_SH1106_12864
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#elif AMAP_OLED_DRIVER == AMAP_OLED_DRIVER_SH1107_128128
static U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

void DisplayRenderer::begin() {
  Wire.begin(AMAP_OLED_SDA_PIN, AMAP_OLED_SCL_PIN);
  u8g2.setI2CAddress(AMAP_OLED_I2C_ADDRESS);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
}

void DisplayRenderer::render(const NavState& state, bool wifiConnected, const String& ip,
                             uint16_t port, unsigned long silenceMs) {
  u8g2.clearBuffer();
  if (!wifiConnected) {
    renderNetwork(false, ip, port);
  } else if (silenceMs > AMAP_STANDBY_MS || !state.active) {
    renderStandby(silenceMs > AMAP_STANDBY_MS ? "等待手机连接" : "等待导航数据",
                  true, ip, port);
  } else if (silenceMs > AMAP_STALE_MS) {
    renderStandby("等待手机数据", true, ip, port);
  } else {
    renderNav(state, silenceMs);
  }
  u8g2.sendBuffer();
}

void DisplayRenderer::renderNetwork(bool wifiConnected, const String& ip, uint16_t port) {
  renderStandby(wifiConnected ? "等待手机连接" : "连接 WiFi 中", wifiConnected, ip, port);
}

void DisplayRenderer::renderStandby(const String& message, bool wifiConnected,
                                    const String& ip, uint16_t port) {
  int width = u8g2.getDisplayWidth();
  setTextFont();
  drawClipped(0, 12, width, "AMap ESP32 Display");
  drawClipped(0, 28, width, message);
  drawClipped(0, 44, width, String("UDP :") + port);
  drawClipped(0, 60, width, wifiConnected ? ("IP " + ip) : "WiFi 未连接");
}

void DisplayRenderer::renderNav(const NavState& state, unsigned long silenceMs) {
  (void)silenceMs;
  int width = u8g2.getDisplayWidth();
  int height = u8g2.getDisplayHeight();
  bool tall = height >= 96;
  int yTop = tall ? 12 : 11;
  int yTurn = tall ? 28 : 24;
  int yNext = tall ? 44 : 37;
  int yEta = tall ? 60 : 50;
  int yBottom = tall ? 76 : 63;

  setSmallFont();
  drawClipped(0, yTop, width, modeLabel(state.mode) + " " + state.road);

  setTextFont();
  String turnLine = turnArrow(state.turn.icon) + state.turn.text;
  if (!state.turn.distanceText.isEmpty()) {
    turnLine += " " + state.turn.distanceText;
  }
  drawClipped(0, yTurn, width, turnLine);

  String next = state.turn.road.isEmpty() ? state.road : state.turn.road;
  drawClipped(0, yNext, width, next);

  String eta = "ETA ";
  if (!state.eta.remainTimeText.isEmpty()) {
    eta += state.eta.remainTimeText;
  }
  if (!state.eta.remainDistanceText.isEmpty()) {
    eta += " " + state.eta.remainDistanceText;
  }
  if (state.speed.current >= 0) {
    eta += " " + String(state.speed.current) + "km/h";
  }
  drawClipped(0, yEta, width, eta);

  String bottom = bottomText(state);
  if (!bottom.isEmpty()) {
    setSmallFont();
    drawClipped(0, yBottom, width, bottom);
  }

  if (tall) {
    if (!state.alert.isEmpty()) {
      drawClipped(0, 92, width, state.alert);
    }
    if (!state.detail.isEmpty()) {
      drawClipped(0, 108, width, state.detail);
    }
  }
}

void DisplayRenderer::setTextFont() {
#if AMAP_USE_CHINESE_FONT
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
#else
  u8g2.setFont(u8g2_font_6x12_tf);
#endif
}

void DisplayRenderer::setSmallFont() {
#if AMAP_USE_CHINESE_FONT
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
#else
  u8g2.setFont(u8g2_font_5x8_tf);
#endif
}

void DisplayRenderer::setLargeFont() {
  u8g2.setFont(u8g2_font_logisoso18_tf);
}

void DisplayRenderer::drawClipped(int x, int y, int maxWidth, const String& text) {
  u8g2.drawUTF8(x, y, clipped(text, maxWidth).c_str());
}

String DisplayRenderer::clipped(String text, int maxWidth) {
  if (maxWidth <= 0) {
    return "";
  }
  while (text.length() > 0 && u8g2.getUTF8Width(text.c_str()) > maxWidth) {
    int cut = text.length() - 1;
    while (cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    text.remove(cut);
  }
  return text;
}

String DisplayRenderer::modeLabel(const String& mode) const {
  if (mode == "nav") {
    return "NAV";
  }
  if (mode == "cruise") {
    return "CRU";
  }
  return "STBY";
}

String DisplayRenderer::turnArrow(int icon) const {
  switch (icon) {
    case 2:
    case 4:
    case 6:
    case 18:
      return "< ";
    case 3:
    case 5:
    case 7:
    case 19:
      return "> ";
    case 8:
      return "U ";
    default:
      return "^ ";
  }
}

String DisplayRenderer::laneText(const NavState& state) const {
  if (state.lane.count == 0) {
    return "";
  }
  String out = "车道 ";
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    if (state.lane.advised[i]) {
      out += "[";
    }
    out += String(state.lane.lanes[i]);
    if (state.lane.advised[i]) {
      out += "]";
    }
    if (i + 1 < state.lane.count) {
      out += " ";
    }
  }
  return out;
}

String DisplayRenderer::lightText(const NavState& state) const {
  if (state.lightCount == 0) {
    return "";
  }
  const LightState& light = state.lights[0];
  String color = light.status == 1 ? "红灯" : (light.status == 4 ? "绿灯" : "黄灯");
  return color + " " + String(light.seconds) + "s";
}

String DisplayRenderer::cameraText(const NavState& state) const {
  if (state.camera.distance < 0 && state.speed.limit <= 0) {
    return "";
  }
  String out;
  if (state.camera.distance >= 0) {
    out += "电子眼 " + String(state.camera.distance) + "m";
  }
  int limit = state.camera.speedLimit > 0 ? state.camera.speedLimit : state.speed.limit;
  if (limit > 0) {
    if (!out.isEmpty()) {
      out += " ";
    }
    out += "限速" + String(limit);
  }
  return out;
}

String DisplayRenderer::bottomText(const NavState& state) const {
  String slots[5] = {
      lightText(state),
      cameraText(state),
      laneText(state),
      state.alert,
      state.detail,
  };
  uint8_t start = (millis() / 2500UL) % 5;
  for (uint8_t i = 0; i < 5; ++i) {
    String candidate = slots[(start + i) % 5];
    if (!candidate.isEmpty()) {
      return candidate;
    }
  }
  return "";
}

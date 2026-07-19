#include "DisplayRenderer.h"

#include <U8g2lib.h>
#include <Wire.h>
#include "Config.h"
#include "NavigationIcons.h"

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

void DisplayRenderer::render(const NavState& state, bool wifiConnected, bool bleConnected,
                             const String& ip, uint16_t port, unsigned long silenceMs) {
  u8g2.clearBuffer();
  if (!wifiConnected && !bleConnected) {
    renderNetwork(false, ip, port);
  } else if (silenceMs > AMAP_STANDBY_MS) {
    renderStandby("等待手机连接",
                  wifiConnected, bleConnected, ip, port);
  } else if (silenceMs > AMAP_STALE_MS) {
    renderStandby("等待手机数据", wifiConnected, bleConnected, ip, port);
  } else if (!state.active && state.music.active) {
    renderMusic(state.music);
  } else if (!state.active) {
    renderStandby("等待导航或音乐", wifiConnected, bleConnected, ip, port);
  } else {
    renderNav(state, silenceMs);
  }
  u8g2.sendBuffer();
}

void DisplayRenderer::renderMusic(const MusicState& music) {
  const int width = u8g2.getDisplayWidth();
  const int height = u8g2.getDisplayHeight();
  const bool tall = height >= 96;

  setTextFont();
  drawClipped(0, 12, width, music.title.isEmpty() ? "网易云音乐" : music.title);
  setSmallFont();
  drawClipped(0, 25, width, (music.playing ? "播放中 " : "已暂停 ") + music.artist);

  setTextFont();
  String visibleLyric = music.highlightedLyric.isEmpty()
                            ? music.lyric : music.highlightedLyric;
  drawClipped(0, 42, width, visibleLyric.isEmpty() ? "暂无歌词" : visibleLyric);
  if (tall) {
    setSmallFont();
    if (!music.translatedLyric.isEmpty()) {
      drawClipped(0, 58, width, music.translatedLyric);
    }
    drawClipped(0, 75, width, music.nextLyric);
  }

  const int progressY = tall ? 91 : 50;
  u8g2.drawFrame(0, progressY, width, 5);
  if (music.durationMs > 0) {
    const int64_t bounded = min<int64_t>(max<int64_t>(0, music.positionMs), music.durationMs);
    const int filled = static_cast<int>(bounded * (width - 2) / music.durationMs);
    if (filled > 0) {
      u8g2.drawBox(1, progressY + 1, filled, 3);
    }
  }
  setSmallFont();
  drawClipped(0, tall ? 108 : 63, width,
              formatTime(music.positionMs) + " / " + formatTime(music.durationMs));
  if (tall && !music.album.isEmpty()) {
    drawClipped(0, 124, width, music.album);
  }
}

void DisplayRenderer::renderNetwork(bool wifiConnected, const String& ip, uint16_t port) {
  renderStandby(wifiConnected ? "等待手机连接" : "AP 配网模式",
                wifiConnected, false, ip, port);
}

void DisplayRenderer::renderStandby(const String& message, bool wifiConnected,
                                    bool bleConnected, const String& ip, uint16_t port) {
  int width = u8g2.getDisplayWidth();
  setTextFont();
  drawClipped(0, 12, width, "AMap ESP32 Display");
  drawClipped(0, 28, width, message);
  drawClipped(0, 44, width, bleConnected ? "BLE connected" : String("UDP :") + port);
  if (wifiConnected) {
    drawClipped(0, 60, width, "IP " + ip);
  } else if (bleConnected && ip != "0.0.0.0") {
    // BLE can be active while the device exposes the AP configuration portal.
    // Keep that address visible instead of hiding it behind "WiFi optional".
    drawClipped(0, 60, width, "IP " + ip);
  } else if (bleConnected) {
    drawClipped(0, 60, width, "WiFi optional");
  } else if (ip != "0.0.0.0") {
    drawClipped(0, 60, width, "AP " + ip);
  } else {
    drawClipped(0, 60, width, "WiFi 未连接");
  }
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
  drawTurnIcon(state.turn.icon, 0, yTurn - 13);
  String turnLine = state.turn.text;
  if (!state.turn.distanceText.isEmpty()) {
    turnLine += " " + state.turn.distanceText;
  }
  drawClipped(19, yTurn, width - 19, turnLine);

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

  String navLyric = state.music.highlightedLyric.isEmpty()
                        ? state.music.lyric : state.music.highlightedLyric;
  String bottom = state.music.active && !navLyric.isEmpty()
                      ? navLyric
                      : bottomText(state);
  if (!bottom.isEmpty()) {
    setSmallFont();
    bool showingCamera = !state.music.active && !cameraText(state).isEmpty() &&
                         bottom == cameraText(state);
    if (showingCamera) {
      drawCameraIcon(state.camera.type, 0, yBottom - 11);
    }
    drawClipped(showingCamera ? 14 : 0, yBottom, width - (showingCamera ? 14 : 0), bottom);
  }

  if (tall) {
    if (state.music.active && !state.music.translatedLyric.isEmpty()) {
      drawClipped(0, 92, width, state.music.translatedLyric);
    } else if (!state.alert.isEmpty()) {
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

void DisplayRenderer::drawTurnIcon(int icon, int x, int y) {
  const NavigationIcons::Bitmap& bitmap = NavigationIcons::turnBitmap(icon);
  u8g2.drawXBMP(x, y, bitmap.width, bitmap.height, bitmap.data);
}

void DisplayRenderer::drawCameraIcon(int type, int x, int y) {
  const NavigationIcons::Bitmap& bitmap = NavigationIcons::cameraBitmap(type);
  u8g2.drawXBMP(x, y, bitmap.width, bitmap.height, bitmap.data);
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
  const uint8_t count = min(state.lightCount, NavState::MAX_LIGHTS);
  const uint8_t pageCount = (count + 1) / 2;
  const uint8_t start = ((millis() / 2500UL) % pageCount) * 2;
  const uint8_t end = min(static_cast<uint8_t>(start + 2), count);
  String out;
  for (uint8_t i = start; i < end; ++i) {
    const LightState& light = state.lights[i];
    if (!out.isEmpty()) {
      out += " ";
    }
#if AMAP_USE_CHINESE_FONT
    const char* direction = light.dir == 0 ? "掉" :
                            light.dir == 1 ? "左" :
                            (light.dir == 2 || light.dir == 3) ? "右" :
                            light.dir == 4 ? "直" :
                            (light.dir == 5 || light.dir == 6) ? "左前" : "右前";
    const char* color = light.status == 1 ? "红" : (light.status == 4 ? "绿" : "黄");
    out += String(direction) + color + String(light.seconds);
#else
    const char* direction = light.dir == 0 ? "U" :
                            light.dir == 1 ? "L" :
                            (light.dir == 2 || light.dir == 3) ? "R" :
                            light.dir == 4 ? "S" :
                            (light.dir == 5 || light.dir == 6) ? "\\" : "/";
    const char* color = light.status == 1 ? "R" : (light.status == 4 ? "G" : "Y");
    out += String(direction) + ":" + color + String(light.seconds);
#endif
  }
  return out;
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

String DisplayRenderer::tmcText(const NavState& state) const {
  if (state.tmc.count == 0 || state.tmc.totalDistance <= 0) {
    return "";
  }
  int slow = 0;
  int congested = 0;
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    if (state.tmc.status[i] == 2) {
      ++slow;
    } else if (state.tmc.status[i] >= 3 && state.tmc.status[i] <= 4) {
      ++congested;
    }
  }
  String out = "路况 " + String(state.tmc.count) + "段";
  if (congested > 0) {
    out += " 拥堵 " + String(congested);
  } else if (slow > 0) {
    out += " 缓行 " + String(slow);
  } else {
    out += " 畅通";
  }
  return out;
}

String DisplayRenderer::routeText(const NavState& state) const {
  String out;
  if (!state.route.destination.isEmpty()) {
    out = "目的地 " + state.route.destination;
  }
  if (state.route.remainingTrafficLights >= 0) {
    if (!out.isEmpty()) {
      out += " ";
    }
    out += "红绿灯 " + String(state.route.remainingTrafficLights);
  }
  return out;
}

String DisplayRenderer::guideText(const NavState& state) const {
  if (!state.guide.exitName.isEmpty()) {
    return "出口 " + state.guide.exitName +
           (state.guide.exitDirection.isEmpty() ? "" : " " + state.guide.exitDirection);
  }
  if (!state.guide.serviceAreaName.isEmpty()) {
    String out = "服务区 " + state.guide.serviceAreaName +
                 (state.guide.serviceAreaDistance.isEmpty() ? "" : " " + state.guide.serviceAreaDistance);
    if (!state.guide.nextServiceAreaName.isEmpty()) {
      out += "  下一 " + state.guide.nextServiceAreaName +
             (state.guide.nextServiceAreaDistance.isEmpty() ? "" : " " + state.guide.nextServiceAreaDistance);
    }
    return out;
  }
  return "";
}

String DisplayRenderer::roadInfoText(const NavState& state) const {
  String out = state.roadInfo.type;
  if (!state.roadInfo.traffic.isEmpty()) {
    if (!out.isEmpty()) {
      out += " ";
    }
    out += state.roadInfo.traffic;
  }
  if (state.roadInfo.crossMap) {
    if (!out.isEmpty()) {
      out += " ";
    }
    out += "路口放大图";
  }
  return out;
}

String DisplayRenderer::bottomText(const NavState& state) const {
  // Countdown information is time-critical. Keep it pinned instead of hiding
  // it in the rotating secondary-info carousel while a light is active.
  if (state.lightCount > 0) {
    return lightText(state);
  }
  String slots[9] = {
      "",
      cameraText(state),
      tmcText(state),
      laneText(state),
      routeText(state),
      guideText(state),
      roadInfoText(state),
      state.alert,
      state.detail,
  };
  uint8_t start = (millis() / 2500UL) % 9;
  for (uint8_t i = 0; i < 9; ++i) {
    String candidate = slots[(start + i) % 9];
    if (!candidate.isEmpty()) {
      return candidate;
    }
  }
  return "";
}

String DisplayRenderer::formatTime(int64_t milliseconds) const {
  if (milliseconds < 0) {
    return "--:--";
  }
  const int64_t totalSeconds = milliseconds / 1000;
  const int minutes = static_cast<int>(totalSeconds / 60);
  const int seconds = static_cast<int>(totalSeconds % 60);
  String result = String(minutes) + ":";
  if (seconds < 10) {
    result += "0";
  }
  return result + String(seconds);
}

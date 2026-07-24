#include "TftFrameRenderer.h"

#include <pgmspace.h>

#include "Config.h"
#include "AlbumArtCache.h"
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

uint16_t musicAccent(int64_t songId) {
  constexpr uint16_t palette[] = {
      0xB32C,  // warm rose
      0x2CB4,  // jade
      0x34BF,  // cyan
      0x8B9F,  // violet
      0xE4A8,  // amber
  };
  const uint64_t value = songId < 0 ? 0 : static_cast<uint64_t>(songId);
  return palette[value % (sizeof(palette) / sizeof(palette[0]))];
}

size_t utf8CharacterBytes(uint8_t firstByte) {
  if ((firstByte & 0x80) == 0) return 1;
  if ((firstByte & 0xE0) == 0xC0) return 2;
  if ((firstByte & 0xF0) == 0xE0) return 3;
  if ((firstByte & 0xF8) == 0xF0) return 4;
  return 1;
}

bool decodeUtf8(const String& text, size_t offset, uint16_t& codepoint, size_t& bytes) {
  if (offset >= text.length()) return false;
  const uint8_t first = static_cast<uint8_t>(text[offset]);
  bytes = utf8CharacterBytes(first);
  if (offset + bytes > text.length()) {
    bytes = 1;
    codepoint = '?';
    return true;
  }
  uint32_t value = bytes == 1 ? first : first & ((1U << (7 - bytes)) - 1U);
  for (size_t index = 1; index < bytes; ++index) {
    const uint8_t next = static_cast<uint8_t>(text[offset + index]);
    if ((next & 0xC0) != 0x80) {
      bytes = 1;
      codepoint = '?';
      return true;
    }
    value = (value << 6) | (next & 0x3F);
  }
  codepoint = value <= 0xFFFF ? static_cast<uint16_t>(value) : 0x25A1;
  return true;
}

const uint8_t* fallbackFont(const uint8_t* primary) {
  return primary == u8g2_font_wqy16_t_gb2312
             ? u8g2_font_b16_t_japanese3
             : u8g2_font_b12_t_japanese3;
}

int16_t glyphWidth(U8G2_FOR_ADAFRUIT_GFX& font, const uint8_t* primary,
                   uint16_t& codepoint) {
  font.setFont(primary);
  int16_t width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
  if (width == 0) {
    font.setFont(fallbackFont(primary));
    width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
  }
  if (width == 0) {
    codepoint = 0x25A1;  // visible replacement box instead of a silent gap
    font.setFont(u8g2_font_unifont_t_symbols);
    width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
  }
  if (width == 0) {
    codepoint = '?';
    font.setFont(primary);
    width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
  }
  return max<int16_t>(0, width);
}

int16_t textWidth(U8G2_FOR_ADAFRUIT_GFX& font, const String& text) {
  const uint8_t* primary = font.u8g2.font;
  int16_t width = 0;
  for (size_t offset = 0; offset < text.length();) {
    uint16_t codepoint = 0;
    size_t bytes = 1;
    if (!decodeUtf8(text, offset, codepoint, bytes)) break;
    width += glyphWidth(font, primary, codepoint);
    offset += bytes;
  }
  font.setFont(primary);
  return width;
}

int16_t drawTextWithFallback(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                             const String& text, uint16_t color) {
  const uint8_t* primary = font.u8g2.font;
  font.setForegroundColor(color);
  int16_t cursor = x;
  for (size_t offset = 0; offset < text.length();) {
    uint16_t codepoint = 0;
    size_t bytes = 1;
    if (!decodeUtf8(text, offset, codepoint, bytes)) break;
    const int16_t width = glyphWidth(font, primary, codepoint);
    font.drawGlyph(cursor, baseline, codepoint);
    cursor += width;
    offset += bytes;
  }
  font.setFont(primary);
  return cursor - x;
}

String clipUtf8ToWidth(U8G2_FOR_ADAFRUIT_GFX& font, const String& text, int16_t maxWidth) {
  if (maxWidth <= 0) return "";
  if (textWidth(font, text) <= maxWidth) return text;

  const String suffix = "...";
  const int16_t contentWidth = max<int16_t>(0, maxWidth - textWidth(font, suffix));
  String result;
  int16_t resultWidth = 0;
  size_t offset = 0;
  while (offset < text.length()) {
    size_t bytes = utf8CharacterBytes(static_cast<uint8_t>(text[offset]));
    if (offset + bytes > text.length()) break;
    bool valid = true;
    for (size_t i = 1; i < bytes; ++i) {
      if ((static_cast<uint8_t>(text[offset + i]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) bytes = 1;

    const String character = text.substring(offset, offset + bytes);
    const int16_t characterWidth = textWidth(font, character);
    if (resultWidth + characterWidth > contentWidth) break;
    result += character;
    resultWidth += characterWidth;
    offset += bytes;
  }
  return result + suffix;
}

class HorizontalClipCanvas : public Adafruit_GFX {
public:
  HorizontalClipCanvas(Adafruit_GFX& target, int16_t left, int16_t right)
      : Adafruit_GFX(target.width(), target.height()), target(target),
        left(left), right(right) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= left && x < right) target.drawPixel(x, y, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override {
    const int16_t clippedLeft = max<int16_t>(x, left);
    const int16_t clippedRight = min<int16_t>(x + width, right);
    if (clippedRight > clippedLeft) {
      target.drawFastHLine(clippedLeft, y, clippedRight - clippedLeft, color);
    }
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override {
    if (x >= left && x < right) target.drawFastVLine(x, y, height, color);
  }

private:
  Adafruit_GFX& target;
  int16_t left;
  int16_t right;
};

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
                               const NavState& state, bool wifiConnected, bool bleConnected,
                               const String& ip, uint16_t port, unsigned long silenceMs,
                               TftViewMode viewMode, MediaControlCommand pressedControl) {
  if (state.music.active) {
    AlbumArtCache::instance().request(state.music.coverUrl, wifiConnected);
  }
  const bool connected = wifiConnected || bleConnected;
  const bool fresh = connected && silenceMs <= AMAP_STANDBY_MS;
  if (viewMode == TftViewMode::Status) {
    renderStandby(display, font, "设备状态", "左右滑切换 · 下滑返回自动",
                  wifiConnected, bleConnected, ip, port);
    return;
  }
  if (viewMode == TftViewMode::Navigation) {
    if (!fresh || !state.active) {
      renderStandby(display, font, "暂无导航数据", "左右滑动可切换界面",
                    wifiConnected, bleConnected, ip, port);
    } else if (state.mode == "cruise") {
      renderCruise(display, font, state);
    } else {
      renderNavigation(display, font, state);
    }
    return;
  }
  if (viewMode == TftViewMode::Music) {
    if (!fresh || !state.music.active) {
      renderStandby(display, font, "暂无音乐数据", "打开网易云音乐后自动更新",
                    wifiConnected, bleConnected, ip, port);
    } else {
      renderMusic(display, font, state.music, pressedControl);
    }
    return;
  }
  if (!wifiConnected && !bleConnected) {
    renderStandby(display, font, "设备配网模式", "连接设备热点后打开配置页面",
                  wifiConnected, bleConnected, ip, port);
  } else if (silenceMs > AMAP_STANDBY_MS) {
    renderStandby(display, font, "等待手机数据", "请打开手机转发器",
                  wifiConnected, bleConnected, ip, port);
  } else if (silenceMs > AMAP_STALE_MS) {
    renderStandby(display, font, "手机数据已暂停", "正在等待新的 UDP / BLE 数据",
                  wifiConnected, bleConnected, ip, port);
  } else if (!state.active && state.music.active) {
    renderMusic(display, font, state.music, pressedControl);
  } else if (!state.active) {
    renderStandby(display, font, "等待导航或音乐", "打开高德导航或网易云音乐",
                  wifiConnected, bleConnected, ip, port);
  } else if (state.mode == "cruise") {
    renderCruise(display, font, state);
    if (state.music.active) {
      drawMusicOverlay(display, font, state.music);
    }
  } else {
    renderNavigation(display, font, state);
    if (state.music.active) {
      drawMusicOverlay(display, font, state.music);
    }
  }
}

void TftFrameRenderer::drawGestureHint(Adafruit_GFX& display,
                                       U8G2_FOR_ADAFRUIT_GFX& font,
                                       TftViewMode viewMode) {
  const char* label = "自动";
  if (viewMode == TftViewMode::Navigation) {
    label = "导航";
  } else if (viewMode == TftViewMode::Music) {
    label = "音乐";
  } else if (viewMode == TftViewMode::Status) {
    label = "状态";
  }
  constexpr int16_t left = 112;
  constexpr int16_t top = 215;
  constexpr int16_t width = 96;
  display.fillRoundRect(left, top, width, 21, 10, kInfoSurface);
  display.drawRoundRect(left, top, width, 21, 10, kCapsuleStroke);
  for (uint8_t index = 0; index < 4; ++index) {
    const bool selected = static_cast<uint8_t>(viewMode) == index;
    display.fillCircle(left + 12 + index * 8, top + 10, selected ? 3 : 2,
                       selected ? kAccent : kMuted);
  }
  drawUtf8(font, left + 49, top + 15, label, kTextSoft);
}

void TftFrameRenderer::renderMusic(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                    const MusicState& music,
                                    MediaControlCommand pressedControl) {
  const unsigned long now = millis();
  const int64_t positionMs = music.positionAt(now);
  const int wordProgressPermille = music.wordProgressAt(now);
  const uint16_t accent = musicAccent(music.songId);
  const uint16_t atmosphere = alphaBlend(kCanvas, accent, 0x32);
  const uint16_t idleLyric = alphaBlend(kCanvas, kText, 0x68);
  const uint16_t distantLyric = alphaBlend(kCanvas, kText, 0x3A);
  display.fillScreen(kCanvas);

  // Refined Now Playing composition scaled to 320x240: album identity on the
  // left, a vertically focused lyric stage on the right, and a dim album-tone
  // atmosphere behind both. No card chrome is used on the music screen.
  display.fillCircle(44, 16, 92, atmosphere);
  display.fillCircle(302, 226, 116, alphaBlend(kCanvas, accent, 0x18));
  display.fillTriangle(0, 240, 172, 240, 0, 112,
                       alphaBlend(kCanvas, accent, 0x16));

  constexpr int16_t coverLeft = 14;
  constexpr int16_t coverTop = 14;
  constexpr int16_t coverSize = 126;
  const uint16_t coverBase = alphaBlend(kSurface, accent, 0x72);
  if (!AlbumArtCache::instance().draw(display, coverLeft, coverTop)) {
    display.fillRoundRect(coverLeft, coverTop, coverSize, coverSize, 12, coverBase);
    display.fillCircle(coverLeft + 63, coverTop + 63, 48,
                       alphaBlend(coverBase, kCanvas, 0x62));
    display.drawCircle(coverLeft + 63, coverTop + 63, 37,
                       alphaBlend(coverBase, kText, 0x45));
    display.drawCircle(coverLeft + 63, coverTop + 63, 27,
                       alphaBlend(coverBase, kText, 0x2A));
    display.fillTriangle(coverLeft + 8, coverTop + 110,
                         coverLeft + 52, coverTop + 24,
                         coverLeft + 102, coverTop + 126,
                         alphaBlend(coverBase, kText, 0x32));
    display.fillCircle(coverLeft + 63, coverTop + 63, 10, accent);
    display.fillCircle(coverLeft + 63, coverTop + 63, 3, kText);
  } else {
    display.drawRoundRect(coverLeft, coverTop, AlbumArtCache::SIZE,
                          AlbumArtCache::SIZE, 10,
                          alphaBlend(kCanvas, kText, 0x42));
  }

  drawClipped(font, 15, 160, 126,
              music.title.isEmpty() ? "网易云音乐" : music.title, kText);
  String byline = music.artist;
  if (!music.album.isEmpty()) {
    byline += (byline.isEmpty() ? "" : " · ") + music.album;
  }
  drawClipped(font, 15, 179, 126, byline, idleLyric);

  constexpr int16_t progressLeft = 15;
  constexpr int16_t progressTop = 190;
  constexpr int16_t progressWidth = 126;
  display.fillRoundRect(progressLeft, progressTop, progressWidth, 3, 1,
                        alphaBlend(kCanvas, kText, 0x25));
  if (music.durationMs > 0) {
    const int64_t bounded = min<int64_t>(max<int64_t>(0, positionMs), music.durationMs);
    const int16_t filled = static_cast<int16_t>(bounded * progressWidth / music.durationMs);
    if (filled > 0) {
      display.fillRoundRect(progressLeft, progressTop, filled, 3, 1, kText);
    }
  }

  // 42 px touch targets provide direct press feedback while keeping the
  // transport controls visually restrained.
  const uint16_t previousColor = pressedControl == MediaControlCommand::Previous
                                     ? accent : idleLyric;
  const uint16_t nextColor = pressedControl == MediaControlCommand::Next
                                 ? accent : idleLyric;
  if (pressedControl == MediaControlCommand::Previous) {
    display.fillCircle(39, 213, 17, alphaBlend(kCanvas, accent, 0x35));
  }
  if (pressedControl == MediaControlCommand::Next) {
    display.fillCircle(123, 213, 17, alphaBlend(kCanvas, accent, 0x35));
  }
  display.fillTriangle(35, 213, 43, 207, 43, 219, previousColor);
  const uint16_t playSurface = pressedControl == MediaControlCommand::PlayPause
                                   ? accent : alphaBlend(kCanvas, kText, 0xE6);
  display.fillCircle(77, 213, 16, playSurface);
  if (music.playing) {
    display.fillRect(72, 206, 3, 14, kCanvas);
    display.fillRect(79, 206, 3, 14, kCanvas);
  } else {
    display.fillTriangle(73, 205, 73, 221, 84, 213, kCanvas);
  }
  display.fillTriangle(119, 207, 119, 219, 127, 213, nextColor);
  drawUtf8(font, 15, 238, formatTime(positionMs), distantLyric);
  const String duration = formatTime(music.durationMs);
  drawUtf8(font, 141 - textWidth(font, duration), 238,
           duration, distantLyric);

  constexpr int16_t lyricLeft = 163;
  constexpr int16_t lyricWidth = 147;
  drawClipped(font, lyricLeft, 29, lyricWidth, music.previousLyric, distantLyric);

  const String lyric = music.lyric.isEmpty() ? "暂无歌词" : music.lyric;
  font.setFont(u8g2_font_wqy16_t_gb2312);
  if (music.highlightedLyric.isEmpty() && music.currentWord.isEmpty()) {
    drawTimedScrollingLine(font, lyricLeft, 82, lyricWidth, lyric,
                           positionMs, music.lineStartMs,
                           music.lineDurationMs, kText);
  } else {
    drawKaraokeLine(font, lyricLeft, 82, lyricWidth, lyric,
                    music.highlightedLyric, music.currentWord,
                    wordProgressPermille, idleLyric, kText);
  }
  font.setFont(u8g2_font_wqy12_t_gb2312);
  if (!music.translatedLyric.isEmpty()) {
    drawTimedScrollingLine(font, lyricLeft, 108, lyricWidth,
                           music.translatedLyric, positionMs,
                           music.lineStartMs, music.lineDurationMs,
                           alphaBlend(kCanvas, kText, 0x82));
  }
  drawClipped(font, lyricLeft, 159, lyricWidth, music.nextLyric, distantLyric);
  if (!music.nextLyric.isEmpty()) {
    display.fillCircle(lyricLeft, 185, 2, alphaBlend(kCanvas, kText, 0x28));
    display.fillCircle(lyricLeft + 8, 185, 2, alphaBlend(kCanvas, kText, 0x1C));
    display.fillCircle(lyricLeft + 16, 185, 2, alphaBlend(kCanvas, kText, 0x12));
  }
}

void TftFrameRenderer::drawMusicOverlay(Adafruit_GFX& display,
                                        U8G2_FOR_ADAFRUIT_GFX& font,
                                        const MusicState& music) {
  constexpr int16_t top = 188;
  constexpr int16_t left = 7;
  const int16_t width = display.width() - 14;
  display.fillRoundRect(left, top, width, 46, 10, kInfoSurface);
  display.drawRoundRect(left, top, width, 46, 10, kCapsuleStroke);
  display.fillCircle(18, top + 15, 4, music.playing ? kGreen : kYellow);

  String current = music.lyric;
  if (current.isEmpty()) {
    current = music.title.isEmpty() ? "暂无歌词" : music.title;
  }
  const unsigned long now = millis();
  const int64_t positionMs = music.positionAt(now);
  if (music.highlightedLyric.isEmpty() && music.currentWord.isEmpty()) {
    drawTimedScrollingLine(font, 29, top + 19, display.width() - 45,
                           current, positionMs, music.lineStartMs,
                           music.lineDurationMs, kAccent);
  } else {
    drawKaraokeLine(font, 29, top + 19, display.width() - 45, current,
                    music.highlightedLyric, music.currentWord,
                    music.wordProgressAt(now), kTextSoft, kAccent);
  }

  String secondary = music.translatedLyric;
  if (secondary.isEmpty()) {
    secondary = music.nextLyric;
  }
  if (secondary.isEmpty()) {
    secondary = music.artist;
  }
  if (!music.translatedLyric.isEmpty()) {
    drawTimedScrollingLine(font, 18, top + 39, display.width() - 36,
                           secondary, positionMs, music.lineStartMs,
                           music.lineDurationMs, kTextSoft);
  } else {
    drawClipped(font, 18, top + 39, display.width() - 36, secondary, kTextSoft);
  }
}

void TftFrameRenderer::drawShell(Adafruit_GFX& display) {
  display.fillScreen(kCanvas);
  display.fillRoundRect(3, 3, display.width() - 6, display.height() - 6, 12, kSurface);
}

void TftFrameRenderer::renderStandby(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                     const String& title, const String& detail,
                                     bool wifiConnected, bool bleConnected, const String& ip,
                                     uint16_t port) {
  drawShell(display);
  display.drawRoundRect(4, 4, display.width() - 8, display.height() - 8, 12, kCapsuleStroke);

  // Brand header and a small geometric navigation mark.
  display.fillRoundRect(14, 13, 40, 40, 10, kAccent);
  display.fillTriangle(34, 20, 23, 44, 34, 38, kText);
  display.fillTriangle(34, 20, 45, 44, 34, 38, kTextSoft);
  drawUtf8(font, 64, 31, "AMAP NAV", kText);
  drawUtf8(font, 64, 47, "ESP32-S3 · ST7789", kMuted);

  display.fillRoundRect(14, 64, display.width() - 28, 76, 12, kInfoSurface);
  display.fillCircle(31, 84, 5, (!wifiConnected && !bleConnected) ? kYellow : kAccent);
  drawUtf8(font, 44, 89, title, kText);
  drawClipped(font, 26, 116, display.width() - 52, detail, kTextSoft);
  display.fillRect(26, 126, display.width() - 52, 1, kDivider);

  const uint16_t wifiColor = wifiConnected ? kGreen : kMuted;
  const uint16_t bleColor = bleConnected ? kGreen : kMuted;
  display.fillRoundRect(14, 151, 88, 28, 14, kCapsule);
  display.fillCircle(28, 165, 4, wifiColor);
  drawUtf8(font, 38, 170, wifiConnected ? "Wi-Fi 在线" : "Wi-Fi 离线", kTextSoft);
  display.fillRoundRect(108, 151, 86, 28, 14, kCapsule);
  display.fillCircle(122, 165, 4, bleColor);
  drawUtf8(font, 132, 170, bleConnected ? "BLE 在线" : "BLE 等待", kTextSoft);
  display.fillRoundRect(200, 151, 106, 28, 14, kCapsule);
  drawUtf8(font, 214, 170, String("UDP ") + port, kTextSoft);

  display.fillRoundRect(14, 190, display.width() - 28, 34, 9, kInfoSurface);
  const String address = ip.isEmpty() || ip == "0.0.0.0" ? "等待网络地址" : String("访问 ") + ip;
  drawClipped(font, 26, 212, display.width() - 52, address, kTextSoft);
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
  } else if (state.camera.distance >= 0) {
    drawCameraPill(display, font, state, 214, 5, 58);
  }
  drawSpeedLimitSign(display, state, 294, 30);

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
  } else if (state.camera.distance >= 0) {
    drawCameraPill(display, font, state, 211, 8, 61);
  }
  drawSpeedLimitSign(display, state, 294, 33);
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
  const int16_t bottomBaseline = top + height - 10;
  const int16_t serviceLeft = 174;
  if (!destination.isEmpty()) {
    drawUtf8(font, 18, bottomBaseline, "DEST", kMuted);
    drawClipped(font, 56, bottomBaseline, serviceLeft - 64, destination, kMuted);
  }
  // Keep service-area information in the lower-right slot. It never occupies
  // the ETA/distance row above it.
  if (!state.guide.serviceAreaName.isEmpty()) {
    String serviceArea = state.guide.serviceAreaName;
    if (!state.guide.serviceAreaDistance.isEmpty()) {
      serviceArea += " " + state.guide.serviceAreaDistance;
    }
    drawClipped(font, serviceLeft, bottomBaseline, display.width() - serviceLeft - 12,
                serviceArea, kTextSoft);
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
  drawTextWithFallback(font, x, baseline, text, color);
}

void TftFrameRenderer::drawClipped(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x, int16_t baseline,
                                   int16_t maxWidth, const String& text, uint16_t color) {
  drawUtf8(font, x, baseline, clipUtf8ToWidth(font, text, maxWidth), color);
}

void TftFrameRenderer::drawKaraokeLine(U8G2_FOR_ADAFRUIT_GFX& font, int16_t x,
                                       int16_t baseline, int16_t maxWidth,
                                       const String& text,
                                       const String& highlighted,
                                       const String& currentWord,
                                       int wordProgressPermille,
                                       uint16_t idleColor,
                                       uint16_t activeColor) {
  if (text.isEmpty()) return;
  size_t prefixBytes = 0;
  const size_t possiblePrefix = min(text.length(), highlighted.length());
  while (prefixBytes < possiblePrefix && text[prefixBytes] == highlighted[prefixBytes]) {
    ++prefixBytes;
  }
  while (prefixBytes > 0 && prefixBytes < text.length() &&
         (static_cast<uint8_t>(text[prefixBytes]) & 0xC0) == 0x80) {
    --prefixBytes;
  }
  const String safeHighlight = text.substring(0, prefixBytes);
  size_t start = 0;
  const int16_t anchor = max<int16_t>(12, maxWidth * 2 / 3);
  while (start < safeHighlight.length() &&
         textWidth(font, safeHighlight.substring(start)) > anchor) {
    start += utf8CharacterBytes(static_cast<uint8_t>(safeHighlight[start]));
  }
  String visible = text.substring(min(start, text.length()));
  String visibleHighlight = start < safeHighlight.length()
                                ? safeHighlight.substring(start) : String();
  drawClipped(font, x, baseline, maxWidth, visible,
              visibleHighlight.isEmpty() ? activeColor : idleColor);
  if (visibleHighlight.isEmpty()) return;

  const bool currentWordAligned = !currentWord.isEmpty() &&
                                  visibleHighlight.endsWith(currentWord);
  if (!currentWordAligned) {
    drawClipped(font, x, baseline, maxWidth, visibleHighlight, activeColor);
    return;
  }

  const String completed = visibleHighlight.substring(
      0, visibleHighlight.length() - currentWord.length());
  drawClipped(font, x, baseline, maxWidth, completed, activeColor);
  const int16_t wordX = x + textWidth(font, completed);
  const int16_t wordWidth = textWidth(font, currentWord);
  const int16_t activeWidth = static_cast<int16_t>(
      (static_cast<int32_t>(wordWidth) * constrain(wordProgressPermille, 0, 1000) + 999) /
      1000);
  if (activeWidth <= 0 || font.u8g2.gfx == nullptr) return;

  const uint8_t* primary = font.u8g2.font;
  HorizontalClipCanvas clipped(*font.u8g2.gfx, wordX, wordX + activeWidth);
  U8G2_FOR_ADAFRUIT_GFX clippedFont;
  clippedFont.begin(clipped);
  clippedFont.setFontMode(1);
  clippedFont.setFont(primary);
  drawTextWithFallback(clippedFont, wordX, baseline, currentWord, activeColor);
}

void TftFrameRenderer::drawTimedScrollingLine(U8G2_FOR_ADAFRUIT_GFX& font,
                                               int16_t x, int16_t baseline,
                                               int16_t maxWidth,
                                               const String& text,
                                               int64_t positionMs,
                                               int64_t lineStartMs,
                                               int64_t lineDurationMs,
                                               uint16_t color) {
  if (text.isEmpty() || maxWidth <= 0 || font.u8g2.gfx == nullptr) return;
  const int16_t width = textWidth(font, text);
  if (width <= maxWidth) {
    drawUtf8(font, x, baseline, text, color);
    return;
  }

  int progress = 0;
  if (lineStartMs >= 0 && lineDurationMs > 0) {
    progress = constrain(static_cast<int>(
        (positionMs - lineStartMs) * 1000 / lineDurationMs), 0, 1000);
  }
  // Keep a brief readable lead-in, then finish before the next line arrives.
  const int scrollProgress = constrain((progress - 60) * 1000 / 820, 0, 1000);
  const int16_t offset = static_cast<int16_t>(
      static_cast<int32_t>(width - maxWidth) * scrollProgress / 1000);

  const uint8_t* primary = font.u8g2.font;
  HorizontalClipCanvas clipped(*font.u8g2.gfx, x, x + maxWidth);
  U8G2_FOR_ADAFRUIT_GFX clippedFont;
  clippedFont.begin(clipped);
  clippedFont.setFontMode(1);
  clippedFont.setFont(primary);
  drawTextWithFallback(clippedFont, x - offset, baseline, text, color);
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
  drawCameraIcon(display, state.camera.type, x + 5, y + 5, kText, kCapsule);
  drawClipped(font, x + 4, y + 47, width - 8, String(state.camera.distance) + "m", kText);
}

void TftFrameRenderer::drawSpeedLimitSign(Adafruit_GFX& display, const NavState& state,
                                           int16_t centerX, int16_t centerY) {
  if (state.speed.limit <= 0) return;
  display.fillCircle(centerX, centerY, 23, kText);
  display.fillCircle(centerX, centerY, 20, kRed);
  display.fillCircle(centerX, centerY, 16, kText);
  const String value = String(state.speed.limit);
  // Three-digit limits such as 120 need a smaller scale to stay inside the sign.
  const uint8_t scale = value.length() >= 3 ? 2 : 3;
  const int16_t textWidth = value.length() * 6 * scale;
  drawBig(display, centerX - textWidth / 2, centerY - 4 * scale, value, scale, kCanvas);
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
  drawTrafficPill(display, light, 270 - trafficPillWidth(light, false), 5, false);
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

String TftFrameRenderer::formatTime(int64_t milliseconds) {
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
  result += String(seconds);
  return result;
}

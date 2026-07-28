#include "TftFrameRenderer.h"

#include <pgmspace.h>

#include "Config.h"
#include "AlbumArtCache.h"
#include "DisplayPreferences.h"
#include "NavigationIcons.h"
#include "NaviLinkIcons.h"

// U8g2_for_Adafruit_GFX exposes a compact font declaration header, while the
// linked U8g2 archive also provides these larger CJK/Hangul font blocks.
extern const uint8_t u8g2_font_unifont_t_gb2312[];
extern const uint8_t u8g2_font_unifont_t_korean1[];
extern const uint8_t u8g2_font_unifont_t_korean2[];

void TftRenderRegions::add(int16_t x, int16_t y, int16_t width, int16_t height) {
  // Dirty detection and transfer operate on 16x8 tiles. Restore the complete
  // boundary tiles as well, otherwise pixels just outside an exact overlay
  // rectangle can come from a stale rotated work buffer and be transferred
  // together with the overlay (most visible around the FPS and gesture pills).
  constexpr int16_t kTileWidth = 16;
  constexpr int16_t kTileHeight = 8;
  if (width <= 0 || height <= 0) return;
  const int16_t clippedLeft = max<int16_t>(0, x);
  const int16_t clippedTop = max<int16_t>(0, y);
  const int16_t clippedRight = min<int16_t>(AMAP_TFT_WIDTH, x + width);
  const int16_t clippedBottom = min<int16_t>(AMAP_TFT_HEIGHT, y + height);
  if (clippedRight <= clippedLeft || clippedBottom <= clippedTop) return;
  const int16_t left = clippedLeft / kTileWidth * kTileWidth;
  const int16_t top = clippedTop / kTileHeight * kTileHeight;
  const int16_t right = min<int16_t>(AMAP_TFT_WIDTH,
      (clippedRight + kTileWidth - 1) / kTileWidth * kTileWidth);
  const int16_t bottom = min<int16_t>(AMAP_TFT_HEIGHT,
      (clippedBottom + kTileHeight - 1) / kTileHeight * kTileHeight);
  if (right <= left || bottom <= top) return;

  TftRenderRect incoming;
  incoming.x = left;
  incoming.y = top;
  incoming.width = static_cast<int16_t>(right - left);
  incoming.height = static_cast<int16_t>(bottom - top);
  // Coalesce transitively.  Merging into only the first matching rectangle
  // can leave a later rectangle overlapping the enlarged result, which costs
  // another address-window transaction and makes the reported pixel count
  // larger than the area actually composed.
  for (uint8_t i = 0; i < count;) {
    const TftRenderRect current = rects[i];
    const int16_t currentRight = current.x + current.width;
    const int16_t currentBottom = current.y + current.height;
    const int16_t incomingRight = incoming.x + incoming.width;
    const int16_t incomingBottom = incoming.y + incoming.height;
    const int16_t mergedLeft = min(current.x, incoming.x);
    const int16_t mergedTop = min(current.y, incoming.y);
    const int16_t mergedRight = max(currentRight, incomingRight);
    const int16_t mergedBottom = max(currentBottom, incomingBottom);
    const int16_t overlapWidth = max<int16_t>(0,
        min(currentRight, incomingRight) - max(current.x, incoming.x));
    const int16_t overlapHeight = max<int16_t>(0,
        min(currentBottom, incomingBottom) - max(current.y, incoming.y));
    const uint32_t unionArea =
        static_cast<uint32_t>(current.width) * current.height +
        static_cast<uint32_t>(incoming.width) * incoming.height -
        static_cast<uint32_t>(overlapWidth) * overlapHeight;
    const uint32_t boundingArea =
        static_cast<uint32_t>(mergedRight - mergedLeft) *
        static_cast<uint32_t>(mergedBottom - mergedTop);
    // Merge only when the bounding box contains no uncovered corner.  A lyric
    // region touching the FPS tile, for example, forms an L-shape; merging its
    // bounding box would submit the untouched gap from a stale work buffer.
    if (boundingArea == unionArea) {
      incoming.x = mergedLeft;
      incoming.y = mergedTop;
      incoming.width = mergedRight - mergedLeft;
      incoming.height = mergedBottom - mergedTop;
      rects[i] = rects[--count];
      i = 0;
      continue;
    }
    ++i;
  }
  if (count < MAX_RECTS) {
    rects[count++] = incoming;
    return;
  }

  // Correctness wins if an unusually fragmented update exceeds the fixed
  // rectangle budget.  Silently dropping the last region leaves stale touch
  // feedback or text on screen; one full-screen compose is rare and safe.
  count = 1;
  rects[0].x = 0;
  rects[0].y = 0;
  rects[0].width = AMAP_TFT_WIDTH;
  rects[0].height = AMAP_TFT_HEIGHT;
}

bool TftRenderRegions::contains(int16_t x, int16_t y) const {
  if (count == 0) return false;
  for (uint8_t i = 0; i < count; ++i) {
    const TftRenderRect& rect = rects[i];
    if (x >= rect.x && x < rect.x + rect.width &&
        y >= rect.y && y < rect.y + rect.height) return true;
  }
  return false;
}

bool TftRenderRegions::intersects(int16_t x, int16_t y, int16_t width,
                                  int16_t height) const {
  if (count == 0 || width <= 0 || height <= 0) return false;
  for (uint8_t i = 0; i < count; ++i) {
    const TftRenderRect& rect = rects[i];
    if (x < rect.x + rect.width && x + width > rect.x &&
        y < rect.y + rect.height && y + height > rect.y) return true;
  }
  return false;
}

uint32_t TftRenderRegions::pixelCount() const {
  uint32_t pixels = 0;
  for (uint8_t i = 0; i < count; ++i) {
    pixels += static_cast<uint32_t>(rects[i].width) * rects[i].height;
  }
  return pixels;
}

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
constexpr uint16_t kPurple = 0x715C;        // soft violet for personal apps
constexpr uint16_t kOrange = 0xFC40;        // warm, high-visibility utility accent

bool regionVisible(const TftRenderRegions* regions, int16_t x, int16_t y,
                   int16_t width, int16_t height) {
  return regions == nullptr || regions->intersects(x, y, width, height);
}

void fillRenderRegions(Adafruit_GFX& display, uint16_t color,
                       const TftRenderRegions* regions) {
  if (regions == nullptr || regions->count == 0) {
    display.fillScreen(color);
    return;
  }
  for (uint8_t index = 0; index < regions->count; ++index) {
    const TftRenderRect& rect = regions->rects[index];
    display.fillRect(rect.x, rect.y, rect.width, rect.height, color);
  }
}

bool drawAlbumBackdrop(Adafruit_GFX& display, const TftRenderRegions* regions,
                       uint8_t sourceOpacity, uint16_t background) {
  AlbumArtCache& cache = AlbumArtCache::instance();
  if (regions == nullptr || regions->count == 0) {
    return cache.drawBlurredRegion(display, 0, 0, display.width(), display.height(),
                                   sourceOpacity, background);
  }
  bool ready = true;
  for (uint8_t index = 0; index < regions->count; ++index) {
    const TftRenderRect& rect = regions->rects[index];
    ready = cache.drawBlurredRegion(display, rect.x, rect.y, rect.width, rect.height,
                                    sourceOpacity, background) && ready;
  }
  return ready;
}

int utf8CodePointCount(const String& text) {
  int count = 0;
  for (size_t index = 0; index < text.length(); ++index) {
    if ((static_cast<uint8_t>(text[index]) & 0xC0U) != 0x80U) ++count;
  }
  return count;
}

int utf8ByteOffset(const String& text, int codePoints) {
  int seen = 0;
  size_t index = 0;
  while (index < text.length() && seen < codePoints) {
    if ((static_cast<uint8_t>(text[index]) & 0xC0U) != 0x80U) ++seen;
    ++index;
    while (index < text.length() && (static_cast<uint8_t>(text[index]) & 0xC0U) == 0x80U) {
      ++index;
    }
  }
  return static_cast<int>(index);
}

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

String airQualityLabel(int value) {
  if (value < 0) return "等待更新";
  if (value <= 50) return "优";
  if (value <= 100) return "良";
  if (value <= 150) return "敏感人群注意";
  if (value <= 200) return "不健康";
  if (value <= 300) return "很不健康";
  return "危险";
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

int16_t glyphWidth(U8G2_FOR_ADAFRUIT_GFX& font, const uint8_t* primary,
                   uint16_t& codepoint) {
  font.setFont(primary);
  int16_t width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
  if (width == 0) {
    // WQY covers the primary Chinese UI. These fallbacks add remaining CJK
    // glyphs and both Hangul blocks without silently dropping weather names.
    const uint8_t* const fallbacks[] = {
        u8g2_font_unifont_t_gb2312,
        // The compact WQY/GB2312 fonts omit punctuation such as the middle
        // dot used between artist/album and temperature/condition.  The
        // symbols font is already linked for the replacement-box glyph and
        // also covers degree and multiplication signs, so using it here adds
        // coverage without replacing the UI font or increasing runtime RAM.
        u8g2_font_unifont_t_symbols,
        u8g2_font_unifont_t_korean1,
        u8g2_font_unifont_t_korean2,
        u8g2_font_b16_t_japanese3,
    };
    for (const uint8_t* fallback : fallbacks) {
      font.setFont(fallback);
      width = u8g2_GetGlyphWidth(&font.u8g2, codepoint);
      if (width != 0) break;
    }
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
  // Keep glyphs transparent even after a fallback font or clipping canvas has
  // changed the U8g2 state; otherwise its default black background leaks into
  // colored cards and buttons.
  font.setFontMode(1);
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

String clipUtf8Window(U8G2_FOR_ADAFRUIT_GFX& font, const String& text, int16_t maxWidth) {
  if (maxWidth <= 0) return "";
  String result;
  int16_t resultWidth = 0;
  for (size_t offset = 0; offset < text.length();) {
    const size_t bytes = min(utf8CharacterBytes(static_cast<uint8_t>(text[offset])),
                             text.length() - offset);
    const String character = text.substring(offset, offset + bytes);
    const int16_t characterWidth = textWidth(font, character);
    if (resultWidth + characterWidth > maxWidth) break;
    result += character;
    resultWidth += characterWidth;
    offset += bytes;
  }
  return result;
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
                               const WeatherState& weather,
                                 TftViewMode viewMode, MediaControlCommand pressedControl,
                                 int8_t pressedSettingsRow, bool phoneDetail,
                                 uint8_t phoneDetailScroll, bool autoMode,
                                 uint8_t settingsPage, int16_t homeScroll,
                                 bool weatherRetryPressed, MusicPageStyle musicPageStyle,
                                 const DisplayPreferences* preferences,
                                 const TftRenderRegions* regions) {
  const DisplayPreferences defaultPreferences;
  const DisplayPreferences& settings = preferences == nullptr
      ? defaultPreferences : *preferences;
  if (state.music.active) {
    AlbumArtCache::instance().request(state.music.coverUrl, wifiConnected);
  }
  const bool connected = wifiConnected || bleConnected;
  const bool fresh = connected && silenceMs <= AMAP_STANDBY_MS;
  if (viewMode == TftViewMode::Home) {
    renderHome(display, font, state, wifiConnected, bleConnected, ip, port, autoMode,
               homeScroll, weather, regions);
    return;
  }
  if (viewMode == TftViewMode::Weather) {
    renderWeather(display, font, weather, wifiConnected, weatherRetryPressed, regions);
    return;
  }
  if (viewMode == TftViewMode::AutoStatus) {
    renderAutoStatus(display, font, state, wifiConnected, bleConnected, ip, port, autoMode,
                     pressedSettingsRow == 0, regions);
    return;
  }
  if (viewMode == TftViewMode::Settings) {
    renderSettings(display, font, wifiConnected, bleConnected, ip, port, pressedSettingsRow,
                   settingsPage, settings, regions);
    return;
  }
  if (viewMode == TftViewMode::Auto) {
    // TftRenderer normally resolves automatic mode before it reaches this
    // function. Keeping a useful fallback here makes preview rendering safe.
    renderHome(display, font, state, wifiConnected, bleConnected, ip, port, true, 0,
               weather, regions);
    return;
  }
  if (viewMode == TftViewMode::Navigation) {
    if (!fresh || !state.active) {
      renderStandby(display, font, "暂无导航数据", "左右滑动可切换界面",
                    wifiConnected, bleConnected, ip, port);
    } else if (state.mode == "cruise") {
      renderCruise(display, font, state, regions);
    } else {
      renderNavigation(display, font, state, regions);
    }
    return;
  }
  if (viewMode == TftViewMode::Music) {
    if (!fresh || !state.music.active) {
      renderStandby(display, font, "暂无音乐数据", "打开任意音乐播放器后自动更新",
                    wifiConnected, bleConnected, ip, port);
    } else {
      renderMusic(display, font, state.music, pressedControl, musicPageStyle, regions);
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
    renderMusic(display, font, state.music, pressedControl, musicPageStyle, regions);
  } else if (!state.active) {
    renderStandby(display, font, "等待导航或音乐", "打开高德导航或音乐播放器",
                  wifiConnected, bleConnected, ip, port);
  } else if (state.mode == "cruise") {
    renderCruise(display, font, state, regions);
    if (state.music.active) {
      drawMusicOverlay(display, font, state.music);
    }
  } else {
    renderNavigation(display, font, state, regions);
    if (state.music.active) {
      drawMusicOverlay(display, font, state.music);
    }
  }
  if (state.phone.notification.active) {
    drawPhoneOverlay(display, font, state.phone, settings.messageBanners);
  }
}

void TftFrameRenderer::renderHome(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                   const NavState& state, bool wifiConnected, bool bleConnected,
                                   const String& ip, uint16_t port, bool autoMode,
                                   int16_t homeScroll, const WeatherState& weather,
                                   const TftRenderRegions* regions) {
  drawShell(display);
  const bool connected = wifiConnected || bleConnected;
  struct AppTile {
    int16_t left;
    int16_t top;
    const char* app;
    const char* title;
    String detail;
    uint16_t surface;
  };
  const String navDetail = state.active ?
      (state.turn.distanceText.isEmpty() ? "正在导航" : state.turn.distanceText) : "打开导航";
  const String musicDetail = state.music.active ?
      (state.music.title.isEmpty() ? "正在播放" : state.music.title) : "打开音乐";
  const String autoDetail = autoMode ? "服务已开启" : "设备状态";
  const String weatherDetail = !weather.configured ? "在配置页设城市"
      : weather.loading ? "正在更新"
      : weather.valid ? String(weather.temperatureC, 0) + "° · " + weather.condition
      : "等待天气数据";
  const AppTile tiles[] = {
      {12, 55, "map", "导航", navDetail, kExitGreen},
      {166, 55, "music", "音乐", musicDetail, kPurple},
      {12, 128, "weather", "天气", weatherDetail, kOrange},
      {166, 128, "auto", "自动", autoDetail, kAccent},
      {12, 201, "settings", "设置", "连接与状态", kCapsule},
      {166, 201, "display", "显示", "亮度与夜间", kPurple},
  };
  for (const AppTile& tile : tiles) {
    const int16_t top = tile.top - homeScroll;
    if (!regionVisible(regions, tile.left, top, 142, 62)) continue;
    display.fillRoundRect(tile.left, top, 142, 62, 14, kInfoSurface);
    drawAppIcon(display, tile.left + 10, top + 11, 38, tile.app, tile.surface);
    drawUtf8(font, tile.left + 58, top + 25, tile.title, kText);
    drawClipped(font, tile.left + 58, top + 46, 72, tile.detail, kTextSoft);
    if (String(tile.app) == "auto" && autoMode) {
      display.fillCircle(tile.left + 132, top + 10, 5, kGreen);
    }
  }

  if (regionVisible(regions, 12, 11, 296, 32)) {
  display.fillRoundRect(12, 11, 296, 32, 12, kInfoSurface);
  display.fillRoundRect(20, 18, 18, 18, 6, kAccent);
  display.fillTriangle(29, 21, 24, 33, 29, 30, kText);
  display.fillTriangle(29, 21, 34, 33, 29, 30, kTextSoft);
  drawUtf8(font, 48, 31, "AMap Drive", kText);
  drawUtf8(font, 143, 31, "桌面", kMuted);
  display.fillCircle(287, 27, 4, connected ? kGreen : kYellow);
  }
  if (regionVisible(regions, 310, 53, 8, 146)) {
  display.fillRoundRect(312, 55, 4, 142, 2, kCapsule);
  const int16_t thumbTop = 55 + homeScroll * 110 / 33;
  display.fillRoundRect(312, thumbTop, 4, 32, 2, kAccent);
  }
}

void TftFrameRenderer::renderWeather(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                     const WeatherState& weather, bool wifiConnected,
                                     bool retryPressed, const TftRenderRegions* regions) {
  drawShell(display);
  if (regionVisible(regions, 10, 8, 300, 48)) {
  drawAppIcon(display, 14, 13, 38, "weather", kOrange);
  drawUtf8(font, 63, 30, weather.city.isEmpty() ? "天气" : weather.city, kText);
  drawUtf8(font, 63, 47, weather.loading ? "正在更新" : "独立应用 · 每 30 分钟刷新", kMuted);

  }
  if (!regionVisible(regions, 8, 58, 304, 180)) return;
  if (!weather.configured) {
    display.fillRoundRect(12, 65, 296, 92, 15, kInfoSurface);
    drawUtf8(font, 28, 95, "还没有设置城市", kText);
    drawClipped(font, 28, 121, 250, "打开设备配置页，填写城市后自动获取天气", kTextSoft);
    drawUtf8(font, 28, 144, "无需 API Key", kAccent);
    drawUtf8(font, 16, 226, "底部上划返回桌面", kMuted);
    return;
  }
  if (!weather.valid) {
    display.fillRoundRect(12, 65, 296, 112, 15, kInfoSurface);
    drawUtf8(font, 28, 95, wifiConnected ? "等待天气数据" : "等待 Wi-Fi 连接", kText);
    drawClipped(font, 28, 121, 250,
                weather.error.isEmpty() ? "后台会自动重试" : weather.error, kTextSoft);
    if (!weather.loading && !weather.error.isEmpty()) {
      const uint16_t retrySurface = retryPressed ? kAccent : alphaBlend(kInfoSurface, kAccent, 0x60);
      display.fillRoundRect(88, 136, 144, 30, 12, retrySurface);
      display.drawRoundRect(88, 136, 144, 30, 12, alphaBlend(kCanvas, kText, 0x46));
      drawUtf8(font, 126, 156, "重新获取", retryPressed ? kCanvas : kText);
    }
    drawUtf8(font, 16, 226, "底部上划返回桌面", kMuted);
    return;
  }

  const uint16_t atmosphere = weather.isDay ? alphaBlend(kSurface, kOrange, 0x4D)
                                            : alphaBlend(kSurface, kPurple, 0x57);
  display.fillRoundRect(12, 62, 296, 93, 16, atmosphere);
  display.fillCircle(267, 91, 27, weather.isDay ? kOrange : kPurple);
  if (!weather.isDay) display.fillCircle(278, 81, 23, atmosphere);
  drawBig(display, 26, 76, isnan(weather.temperatureC) ? "--" : String(weather.temperatureC, 0), 6, kText);
  drawUtf8(font, 109, 112, "°C", kTextSoft);
  drawUtf8(font, 27, 134, weather.condition, kText);
  drawClipped(font, 105, 134, 128,
              String("体感 ") + String(weather.feelsLikeC, 0) + "°  湿度 " + weather.humidity + "%", kTextSoft);
  drawUtf8(font, 27, 151, String("风 ") + String(weather.windKph, 0) + " km/h", kTextSoft);
  const String airDetail = weather.airQualityValid
      ? String("AQI ") + weather.usAqi + " " + airQualityLabel(weather.usAqi) +
            " · PM2.5 " + String(weather.pm25, 0)
      : "空气质量等待更新";
  drawClipped(font, 116, 151, 174, airDetail,
              weather.airQualityValid && weather.usAqi > 100 ? kYellow : kAccent);

  const char* labels[] = {"今天", "明天", "后天"};
  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t left = 12 + i * 100;
    const WeatherForecastDay& day = weather.days[i];
    display.fillRoundRect(left, 166, 92, 53, 12, kInfoSurface);
    drawUtf8(font, left + 10, 185, labels[i], kMuted);
    drawUtf8(font, left + 10, 204,
             isnan(day.highC) ? "--" : String(day.highC, 0) + "°", kText);
    drawUtf8(font, left + 46, 204,
             isnan(day.lowC) ? "--" : String(day.lowC, 0) + "°", kTextSoft);
    if (day.rainChance >= 0) drawUtf8(font, left + 10, 217, String("降水 ") + day.rainChance + "%", kAccent);
  }
  drawUtf8(font, 16, 235, "底部上划返回桌面", kMuted);
}

void TftFrameRenderer::renderAutoStatus(Adafruit_GFX& display,
                                        U8G2_FOR_ADAFRUIT_GFX& font,
                                        const NavState& state, bool wifiConnected,
                                        bool bleConnected, const String& ip, uint16_t port,
                                        bool autoMode, bool pressed,
                                        const TftRenderRegions* regions) {
  drawShell(display);
  if (regionVisible(regions, 10, 8, 300, 48)) {
  drawAppIcon(display, 16, 14, 36, "auto", kAccent);
  drawUtf8(font, 63, 30, "自动", kText);
  drawUtf8(font, 63, 47, "独立应用 · 设备状态与服务", kMuted);
  }
  const bool connected = wifiConnected || bleConnected;
  if (regionVisible(regions, 8, 58, 304, 56)) {
  display.fillRoundRect(12, 62, 296, 48, 13, kInfoSurface);
  display.fillCircle(29, 86, 6, connected ? kGreen : kYellow);
  drawUtf8(font, 45, 82, connected ? "设备在线" : "等待设备连接", kText);
  drawClipped(font, 45, 101, 244,
              wifiConnected ? String("Wi-Fi · ") + ip + ":" + port
                            : (bleConnected ? "BLE 已连接 · Wi-Fi 可选" : "UDP / BLE 均未连接"),
              kTextSoft);
  }
  if (regionVisible(regions, 8, 116, 304, 49)) {
  display.fillRoundRect(12, 120, 296, 41, 12, kInfoSurface);
  drawUtf8(font, 25, 141, "后台服务", kMuted);
  const String source = state.active ? "导航" : (state.music.active ? "音乐" : "桌面");
  drawUtf8(font, 112, 141, source, kText);
  drawUtf8(font, 25, 157, "不会切换到导航界面", kMuted);
  }
  if (regionVisible(regions, 8, 173, 304, 51)) {
  const uint16_t actionSurface = autoMode ? kExitGreen : kAccent;
  display.fillRoundRect(12, 177, 296, 43, 14, pressed ? kText : actionSurface);
  drawUtf8(font, 35, 204, autoMode ? "停止自动接管" : "启动自动接管",
           pressed ? kCanvas : kText);
  drawUtf8(font, 205, 204, autoMode ? "运行中" : "未开启",
           pressed ? kCanvas : kTextSoft);
  }
  drawUtf8(font, 16, 235, "底部上划返回桌面", kMuted);
}

void TftFrameRenderer::renderPhoneSheet(Adafruit_GFX& display,
                                        U8G2_FOR_ADAFRUIT_GFX& font,
                                        const PhoneState& phone, bool wifiConnected,
                                        bool bleConnected, bool detail,
                                        uint8_t detailScroll,
                                        const TftRenderRegions* regions) {
  drawShell(display);
  display.fillRoundRect(140, 8, 40, 4, 2, kMuted);
  drawUtf8(font, 16, 29, "通知中心", kText);
  drawUtf8(font, 242, 29, "上滑关闭", kMuted);
  if (!phone.enabled) {
    drawClipped(font, 16, 58, 280, "请在手机 App 中启用手机联动", kTextSoft);
    return;
  }
  if (detail && phone.notification.active) {
    drawClipped(font, 16, 58, 288, phone.notification.app + "  " + phone.notification.title, kText);
    const String& body = phone.notification.body;
    const int total = utf8CodePointCount(body);
    const int lineWidth = 18;
    const int first = min(total, static_cast<int>(detailScroll) * lineWidth);
    for (int line = 0; line < 7; ++line) {
      const int start = first + line * lineWidth;
      if (start >= total) break;
      const int end = min(total, start + lineWidth);
      const int beginOffset = utf8ByteOffset(body, start);
      const int endOffset = utf8ByteOffset(body, end);
      drawClipped(font, 16, 90 + line * 20, 288, body.substring(beginOffset, endOffset), kTextSoft);
    }
    drawUtf8(font, 16, 232, "上下滑动阅读，点按返回", kMuted);
    display.fillCircle(295, 20, 4, (wifiConnected || bleConnected) ? kGreen : kYellow);
    return;
  }
  // A compact dashboard gives the most time-sensitive information the largest
  // visual weight: weather, the next commitment, then the latest notification.
  display.fillRoundRect(12, 41, 296, 62, 14, kInfoSurface);
  const String temperature = isnan(phone.weather.temperatureC)
      ? "--" : String(phone.weather.temperatureC, 1);
  drawBig(display, 24, 49, temperature, 3, kText);
  // drawBig uses Adafruit's ASCII bitmap font, so draw the non-ASCII degree
  // mark through the same U8g2 fallback path as every other UI string.
  drawUtf8(font, 26 + temperature.length() * 18, 62, "°", kText);
  drawClipped(font, 115, 64, 174, phone.weather.condition.isEmpty() ? "等待定位" : phone.weather.condition, kTextSoft);
  const String weatherMeta = phone.weather.aqi >= 0 ? "AQI " + String(phone.weather.aqi) : "天气数据";
  drawUtf8(font, 115, 87, weatherMeta, phone.weather.aqi >= 0 && phone.weather.aqi > 100 ? kYellow : kAccent);
  if (!phone.weather.alert.isEmpty()) {
    display.fillRoundRect(214, 73, 80, 20, 10, kCapsule);
    drawClipped(font, 224, 88, 62, phone.weather.alert, kRed);
  }
  display.fillRoundRect(12, 111, 296, 49, 12, kInfoSurface);
  drawUtf8(font, 24, 130, "下一日程", kMuted);
  const String eventDisplay = phone.calendar.title.isEmpty() ? "未来 24 小时无日程" : phone.calendar.title;
  drawClipped(font, 86, 130, 204, eventDisplay, kText);
  drawClipped(font, 24, 150, 266, phone.calendar.location.isEmpty() ? "暂无地点" : phone.calendar.location, kTextSoft);
  display.fillRoundRect(12, 168, 296, 25, 12, kCapsule);
  String deviceDisplay = phone.device.batteryPercent < 0 ? "手机状态不可用" : String("电量 ") + phone.device.batteryPercent + "%" + (phone.device.charging ? " · 充电中" : "") + "  " + phone.device.network;
  if (phone.device.signalLevel >= 0) deviceDisplay += "  信号 " + String(phone.device.signalLevel);
  drawClipped(font, 24, 186, 268, deviceDisplay, kTextSoft);
  if (phone.notification.active) {
    display.fillRoundRect(12, 200, 296, 32, 12, kInfoSurface);
    drawAppIcon(display, 19, 204, 24, phone.notification.app, kAccent);
    drawClipped(font, 52, 215, 236, phone.notification.app + "  " + phone.notification.title, kText);
    drawClipped(font, 52, 230, 236, phone.notification.body, kTextSoft);
  } else {
    drawClipped(font, 18, 218, 280, "暂无来电或新消息", kMuted);
  }
  display.fillCircle(295, 20, 4, (wifiConnected || bleConnected) ? kGreen : kYellow);
}

void TftFrameRenderer::renderSettings(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                       bool wifiConnected, bool bleConnected, const String& ip,
                                      uint16_t port, int8_t pressedRow, uint8_t settingsPage,
                                      const DisplayPreferences& settings,
                                      const TftRenderRegions* regions) {
  drawShell(display);
  if (settingsPage == 0) {
    drawUtf8(font, 16, 28, "设置", kText);
    drawUtf8(font, 16, 45, "选择类别 · 底部上划返回桌面", kMuted);
    const char* titles[] = {"显示与亮度", "自动与通知", "设备与连接", "开发者选项"};
    const char* details[] = {"亮度、夜间调暗、音乐风格", "自动接管、消息横幅",
                             "Wi-Fi、BLE、UDP 状态", "帧率、渲染诊断"};
    const char* icons[] = {"display", "auto", "device", "settings"};
    const uint16_t colors[] = {kAccent, kPurple, kExitGreen, kYellow};
    for (int8_t row = 0; row < 4; ++row) {
      const int16_t top = 58 + row * 38;
      if (!regionVisible(regions, 8, top - 4, 304, 41)) continue;
      const bool pressed = pressedRow == row;
      display.fillRoundRect(12, top, 296, 33, 10, pressed ? kCapsule : kInfoSurface);
      drawAppIcon(display, 21, top + 4, 25, icons[row], colors[row]);
      drawUtf8(font, 56, top + 16, titles[row], pressed ? kText : kTextSoft);
      drawUtf8(font, 56, top + 29, details[row], kMuted);
      drawUtf8(font, 286, top + 22, ">", pressed ? kText : kMuted);
    }
    display.fillRoundRect(12, 214, 296, 16, 8, kCapsule);
    drawClipped(font, 23, 226, 270,
                String(wifiConnected ? "Wi-Fi 已连接" : "Wi-Fi 离线") + " · " +
                    (bleConnected ? "BLE 已连接" : "BLE 等待"), kTextSoft);
    return;
  }

  display.fillRoundRect(12, 10, 54, 25, 10, kCapsule);
  drawUtf8(font, 25, 28, "< 返回", kTextSoft);
  const bool displayPage = settingsPage == 1;
  const bool behaviorPage = settingsPage == 2;
  const bool developerPage = settingsPage == 4;
  drawUtf8(font, 78, 28,
           displayPage ? "显示与亮度" : (behaviorPage ? "自动与通知" :
           (developerPage ? "开发者选项" : "设备与连接")), kText);
  drawUtf8(font, 16, 52, "底部上划返回桌面", kMuted);
  if (settingsPage == 3) {
    const String statuses[] = {
        String("Wi-Fi  ") + (wifiConnected ? "已连接" : "离线"),
        String("BLE  ") + (bleConnected ? "已连接" : "等待连接"),
        String("UDP  ") + ip + ":" + port,
        String("显示屏  320×240 · ") + (wifiConnected || bleConnected ? "在线" : "配网模式"),
    };
    for (int8_t row = 0; row < 4; ++row) {
      const int16_t top = 65 + row * 36;
      if (!regionVisible(regions, 8, top - 4, 304, 37)) continue;
      display.fillRoundRect(12, top, 296, 29, 10, kInfoSurface);
      display.fillCircle(28, top + 14, 4,
                         row == 0 ? (wifiConnected ? kGreen : kMuted) :
                         row == 1 ? (bleConnected ? kGreen : kMuted) : kAccent);
      drawClipped(font, 42, top + 20, 248, statuses[row], kTextSoft);
    }
    return;
  }

  if (developerPage) {
    if (!regionVisible(regions, 8, 64, 304, 96)) return;
    const bool pressed = pressedRow == 0;
    display.fillRoundRect(12, 70, 296, 35, 11, pressed ? kCapsule : kInfoSurface);
    display.fillCircle(29, 87, 5, pressed ? kText : kYellow);
    drawUtf8(font, 45, 93, "显示帧率", pressed ? kText : kTextSoft);
    display.fillRoundRect(238, 77, 57, 21, 10, pressed ? kText : kSurface);
    drawUtf8(font, 248, 93, settings.showFrameRate ? "开启" : "关闭",
             pressed ? kCanvas : kTextSoft);
    drawUtf8(font, 16, 132, "在右上角显示实时渲染 FPS", kMuted);
    drawUtf8(font, 16, 151, "开启后会持续合成画面，便于性能诊断", kMuted);
    return;
  }

  const String labels[] = {displayPage ? "亮度" : "自动模式",
                           displayPage ? "夜间调暗" : "消息横幅",
                           "音乐风格"};
  const String values[] = {displayPage ? String(settings.brightness) + "%"
                                        : (settings.autoView ? "开启" : "关闭"),
                           displayPage ? (settings.nightDim ? "开启" : "关闭")
                                       : (settings.messageBanners ? "开启" : "关闭"),
                           settings.musicPageStyle == MusicPageStyle::PipWindow
                               ? "PiPWindow"
                               : (settings.musicPageStyle == MusicPageStyle::RefinedNowPlaying
                                      ? "Refined"
                                      : "标准")};
  const int8_t rowCount = displayPage ? 3 : 2;
  for (int8_t row = 0; row < rowCount; ++row) {
    const int16_t top = 70 + row * 42;
    if (!regionVisible(regions, 8, top - 4, 304, 43)) continue;
    const bool pressed = pressedRow == row;
    display.fillRoundRect(12, top, 296, 35, 11, pressed ? kCapsule : kInfoSurface);
    display.fillCircle(29, top + 17, 5, pressed ? kText : kAccent);
    drawUtf8(font, 45, top + 23, labels[row], pressed ? kText : kTextSoft);
    display.fillRoundRect(238, top + 7, 57, 21, 10, pressed ? kText : kSurface);
    drawUtf8(font, 248, top + 23, values[row], pressed ? kCanvas : kTextSoft);
  }
}

void TftFrameRenderer::drawPhoneOverlay(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                         const PhoneState& phone, bool messageBanners) {
  const bool call = phone.notification.kind == "call";
  if (call) {
    display.fillScreen(kCanvas);
    drawUtf8(font, 26, 62, "来电", kAccent);
    drawClipped(font, 26, 112, 268, phone.notification.sender.isEmpty() ? phone.notification.title : phone.notification.sender, kText);
    drawClipped(font, 26, 145, 268, phone.notification.body, kTextSoft);
    drawUtf8(font, 26, 208, "来电结束后自动返回", kMuted);
  } else if (messageBanners && phone.receivedAt != 0 &&
             millis() - phone.receivedAt <= 6000UL) {
    display.fillRoundRect(8, 8, 304, 62, 10, kInfoSurface);
    drawAppIcon(display, 16, 19, 38, phone.notification.app, kAccent);
    drawClipped(font, 64, 31, 230, phone.notification.app + "  " + phone.notification.title, kText);
    drawClipped(font, 64, 55, 230, phone.notification.body, kTextSoft);
  }
}

void TftFrameRenderer::drawAppIcon(Adafruit_GFX& display, int16_t left, int16_t top,
                                   int16_t size, const String& app, uint16_t surface) {
  String name = app;
  name.toLowerCase();
  uint16_t iconSurface = surface;
  if (name.indexOf("music") >= 0 || name.indexOf("音乐") >= 0 ||
      name.indexOf("网易") >= 0 || name.indexOf("qq") >= 0) {
    iconSurface = kPurple;
  } else if (name.indexOf("phone") >= 0 || name.indexOf("消息") >= 0 ||
             name.indexOf("微信") >= 0 || name.indexOf("短信") >= 0) {
    iconSurface = kOrange;
  } else if (name.indexOf("setting") >= 0 || name.indexOf("设置") >= 0) {
    iconSurface = kCapsule;
  } else if (name.indexOf("weather") >= 0 || name.indexOf("天气") >= 0) {
    iconSurface = kOrange;
  } else if (name.indexOf("map") >= 0 || name.indexOf("导航") >= 0 ||
             name.indexOf("高德") >= 0) {
    iconSurface = kExitGreen;
  }
  display.fillRoundRect(left, top, size, size, max<int16_t>(6, size / 4), iconSurface);
  const int16_t centerX = left + size / 2;
  const int16_t centerY = top + size / 2;
  const int16_t inset = max<int16_t>(5, size / 5);
  if (name.indexOf("music") >= 0 || name.indexOf("音乐") >= 0 ||
      name.indexOf("网易") >= 0 || name.indexOf("qq") >= 0) {
    display.fillCircle(centerX - 3, top + size - inset - 2, 4, kText);
    display.drawFastVLine(centerX + 1, top + inset, size - inset * 2, kText);
    display.fillTriangle(centerX + 1, top + inset, centerX + 1, top + inset + 8,
                         centerX + 9, top + inset + 4, kText);
  } else if (name.indexOf("phone") >= 0 || name.indexOf("消息") >= 0 ||
             name.indexOf("微信") >= 0 || name.indexOf("短信") >= 0) {
    display.fillRoundRect(left + inset, top + inset + 2, size - inset * 2, size / 2,
                          5, kText);
    display.fillTriangle(left + inset + 5, top + size / 2 + 2, left + inset + 12,
                         top + size / 2 + 2, left + inset + 7, top + size - inset, kText);
  } else if (name.indexOf("setting") >= 0 || name.indexOf("设置") >= 0) {
    display.drawCircle(centerX, centerY, size / 4, kText);
    display.fillCircle(centerX, centerY, size / 8, kText);
    display.fillRect(centerX - 2, top + inset - 1, 4, 6, kText);
    display.fillRect(centerX - 2, top + size - inset - 5, 4, 6, kText);
    display.fillRect(left + inset - 1, centerY - 2, 6, 4, kText);
    display.fillRect(left + size - inset - 5, centerY - 2, 6, 4, kText);
  } else if (name.indexOf("weather") >= 0 || name.indexOf("天气") >= 0) {
    display.fillCircle(centerX - 5, centerY - 5, size / 6, kText);
    display.fillCircle(centerX - 11, centerY + 5, size / 7, kTextSoft);
    display.fillCircle(centerX, centerY + 3, size / 6, kTextSoft);
    display.fillCircle(centerX + 11, centerY + 7, size / 8, kTextSoft);
    display.fillRoundRect(centerX - 13, centerY + 5, 27, size / 6, size / 12, kTextSoft);
  } else if (name.indexOf("map") >= 0 || name.indexOf("导航") >= 0 ||
             name.indexOf("高德") >= 0) {
    display.fillTriangle(centerX, top + inset, left + size - inset, top + size - inset,
                          centerX, top + size - inset - 5, kText);
    display.fillTriangle(centerX, top + inset, left + inset, top + size - inset,
                          centerX, top + size - inset - 5, kTextSoft);
  } else if (name.indexOf("auto") >= 0 || name.indexOf("自动") >= 0) {
    const int16_t radius = max<int16_t>(6, size / 4);
    display.drawCircle(centerX, centerY, radius, kText);
    display.fillTriangle(centerX + radius + 3, centerY - 2, centerX + radius - 4,
                         centerY - 8, centerX + radius - 4, centerY + 4, kText);
    display.fillCircle(centerX - radius - 2, centerY + radius - 1, 2, kTextSoft);
  } else if (name.indexOf("display") >= 0 || name.indexOf("显示") >= 0) {
    const int16_t frameLeft = left + inset - 1;
    const int16_t frameTop = top + inset + 1;
    const int16_t frameWidth = size - inset * 2 + 2;
    const int16_t frameHeight = size - inset * 2 - 2;
    display.drawRoundRect(frameLeft, frameTop, frameWidth, frameHeight, 3, kText);
    display.fillCircle(centerX, centerY, max<int16_t>(3, size / 9), kTextSoft);
    display.drawFastHLine(centerX - size / 6, top + size - inset + 2, size / 3, kText);
  } else {
    display.fillCircle(centerX, centerY, size / 4, kText);
    display.fillCircle(centerX - size / 7, centerY - 1, 2, iconSurface);
    display.fillCircle(centerX + size / 7, centerY - 1, 2, iconSurface);
    display.fillRoundRect(centerX - size / 7, centerY + 5, size / 3, 3, 2, iconSurface);
  }
}

void TftFrameRenderer::drawGestureHint(Adafruit_GFX& display,
                                       U8G2_FOR_ADAFRUIT_GFX& font,
                                       TftViewMode viewMode) {
  const char* label = "桌面";
  if (viewMode == TftViewMode::Auto) {
    label = "自动";
  } else if (viewMode == TftViewMode::Navigation) {
    label = "导航";
  } else if (viewMode == TftViewMode::Music) {
    label = "音乐";
  } else if (viewMode == TftViewMode::Weather) {
    label = "天气";
  } else if (viewMode == TftViewMode::Settings) {
    label = "设置";
  } else if (viewMode == TftViewMode::AutoStatus) {
    label = "自动";
  }
  constexpr int16_t left = 112;
  constexpr int16_t top = 215;
  constexpr int16_t width = 96;
  display.fillRoundRect(left, top, width, 21, 10, kInfoSurface);
  display.drawRoundRect(left, top, width, 21, 10, kCapsuleStroke);
  drawUtf8(font, left + 31, top + 15, label, kTextSoft);
}

void TftFrameRenderer::drawFrameRate(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                     uint16_t framesPerSecond) {
  constexpr int16_t left = 258;
  constexpr int16_t top = 5;
  display.fillRoundRect(left, top, 57, 18, 7, kCanvas);
  display.drawRoundRect(left, top, 57, 18, 7, kAccent);
  font.setFont(u8g2_font_6x10_tf);
  drawUtf8(font, left + 6, top + 13, String("FPS ") + framesPerSecond, kText);
  // The renderer shares one U8g2 object between frames.  Restore the normal
  // CJK font and transparent text mode so this debug overlay never leaks its
  // ASCII font or opaque background into the next screen.
  font.setFont(u8g2_font_wqy12_t_gb2312);
  font.setFontMode(1);
}

void TftFrameRenderer::renderMusic(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                     const MusicState& music,
                                     MediaControlCommand pressedControl,
                                     MusicPageStyle pageStyle,
                                     const TftRenderRegions* regions) {
  if (pageStyle == MusicPageStyle::PipWindow) {
    renderMusicPipWindow(display, font, music, regions);
    return;
  }
  if (pageStyle == MusicPageStyle::RefinedNowPlaying) {
    renderMusicRefinedNowPlaying(display, font, music, pressedControl, regions);
    return;
  }
  const unsigned long now = millis();
  const int64_t positionMs = music.positionAt(now);
  const int wordProgressPermille = music.wordProgressAt(now);
  const uint16_t accent = musicAccent(music.songId);
  const uint16_t atmosphere = alphaBlend(kCanvas, accent, 0x32);
  const uint16_t idleLyric = alphaBlend(kCanvas, kText, 0x68);
  const uint16_t distantLyric = alphaBlend(kCanvas, kText, 0x3A);
  display.fillScreen(kCanvas);

  // Firmware-standard composition: album identity on the left, a vertically
  // focused lyric stage on the right, and a restrained album-tone atmosphere.
  display.fillCircle(44, 16, 92, atmosphere);
  display.fillCircle(302, 226, 116, alphaBlend(kCanvas, accent, 0x18));
  display.fillTriangle(0, 240, 172, 240, 0, 112,
                       alphaBlend(kCanvas, accent, 0x16));

  constexpr int16_t coverLeft = 14;
  constexpr int16_t coverTop = 14;
  constexpr int16_t coverSize = 126;
  const uint16_t coverBase = alphaBlend(kSurface, accent, 0x72);
  if (regionVisible(regions, coverLeft, coverTop, coverSize + 2, coverSize + 2)) {
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
  }

  if (regionVisible(regions, 10, 145, 136, 40)) {
  drawClipped(font, 15, 160, 126,
              music.title.isEmpty()
                  ? (music.sourceName.isEmpty() ? "音乐播放器" : music.sourceName)
                  : music.title, kText);
  String byline = music.artist;
  if (!music.album.isEmpty()) {
    byline += (byline.isEmpty() ? "" : " · ") + music.album;
  }
  drawClipped(font, 15, 179, 126, byline, idleLyric);
  }

  constexpr int16_t progressLeft = 15;
  constexpr int16_t progressTop = 190;
  constexpr int16_t progressWidth = 126;
  if (regionVisible(regions, 10, 184, 136, 15)) {
  display.fillRoundRect(progressLeft, progressTop, progressWidth, 3, 1,
                        alphaBlend(kCanvas, kText, 0x25));
  if (music.durationMs > 0) {
    const int64_t bounded = min<int64_t>(max<int64_t>(0, positionMs), music.durationMs);
    const int16_t filled = static_cast<int16_t>(bounded * progressWidth / music.durationMs);
    if (filled > 0) {
      display.fillRoundRect(progressLeft, progressTop, filled, 3, 1, kText);
    }
  }
  }

  // 42 px touch targets provide direct press feedback while keeping the
  // transport controls visually restrained.
  if (regionVisible(regions, 10, 196, 136, 44)) {
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
  }

  constexpr int16_t lyricLeft = 163;
  constexpr int16_t lyricWidth = 147;
  constexpr int16_t lyricStageTop = 48;
  if (regionVisible(regions, 158, 39, 157, 121)) {
  // Give each line its own breathing room: previous, active/translation and
  // next lyric use three distinct vertical bands instead of a dense stack.
  drawClipped(font, lyricLeft, lyricStageTop, lyricWidth, music.previousLyric, distantLyric);

  const String lyric = music.lyric.isEmpty() ? "暂无歌词" : music.lyric;
  font.setFont(u8g2_font_wqy16_t_gb2312);
  if (music.highlightedLyric.isEmpty() && music.currentWord.isEmpty()) {
    drawTimedScrollingLine(font, lyricLeft, lyricStageTop + 46, lyricWidth, lyric,
                           positionMs, music.lineStartMs,
                           music.lineDurationMs, kText);
  } else {
    drawKaraokeLine(font, lyricLeft, lyricStageTop + 46, lyricWidth, lyric,
                    music.highlightedLyric, music.currentWord,
                    wordProgressPermille, idleLyric, kText);
  }
  font.setFont(u8g2_font_wqy12_t_gb2312);
  if (!music.translatedLyric.isEmpty()) {
    drawTimedScrollingLine(font, lyricLeft, lyricStageTop + 70, lyricWidth,
                           music.translatedLyric, positionMs,
                           music.lineStartMs, music.lineDurationMs,
                           alphaBlend(kCanvas, kText, 0x82));
  }
  drawClipped(font, lyricLeft, lyricStageTop + 104, lyricWidth, music.nextLyric, distantLyric);
  }
}

void TftFrameRenderer::renderMusicRefinedNowPlaying(
    Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
    const MusicState& music, MediaControlCommand pressedControl,
    const TftRenderRegions* regions) {
  const unsigned long now = millis();
  const int64_t positionMs = music.positionAt(now);
  const uint16_t fallbackTone = musicAccent(music.songId);
  const uint16_t albumTone = AlbumArtCache::instance().dominantColor(fallbackTone);
  const uint16_t backdrop = alphaBlend(kCanvas, albumTone, 0x54);
  // Refined does not use raw dominant-cover pixels for text. It selects a
  // representative source color, then raises it to light tonal shades so the
  // active lyric stays cover-matched and readable on the dim backdrop.
  const uint16_t shade1 = alphaBlend(albumTone, kText, 0xA8);
  const uint16_t shade2 = alphaBlend(albumTone, kText, 0xD4);
  const uint16_t strong = shade2;
  const uint16_t medium = alphaBlend(backdrop, shade1, 0xB4);
  const uint16_t faint = alphaBlend(backdrop, shade1, 0x62);
  const uint16_t accent = shade1;

  if (!drawAlbumBackdrop(display, regions, 104, kCanvas)) {
    fillRenderRegions(display, backdrop, regions);
  }

  // The reference page keeps album identity and metadata on the left while a
  // vertically focused lyric rail occupies the right half. Its cover-derived
  // background is intentionally edge-to-edge rather than enclosed in a card.
  constexpr int16_t coverLeft = 18;
  constexpr int16_t coverTop = 18;
  constexpr int16_t coverSize = 116;
  if (regionVisible(regions, 12, 12, 132, 130)) {
    display.fillRoundRect(coverLeft + 5, coverTop + 7, coverSize, coverSize, 12,
                          alphaBlend(kCanvas, albumTone, 0x64));
    if (!AlbumArtCache::instance().draw(display, coverLeft, coverTop, coverSize)) {
      display.fillRoundRect(coverLeft, coverTop, coverSize, coverSize, 12,
                            alphaBlend(kCanvas, albumTone, 0xB0));
      display.drawCircle(coverLeft + 58, coverTop + 58, 38, medium);
      display.fillCircle(coverLeft + 58, coverTop + 58, 7, strong);
    }
    display.drawRoundRect(coverLeft, coverTop, coverSize, coverSize, 12,
                          alphaBlend(albumTone, kText, 0x58));
  }

  if (regionVisible(regions, 12, 140, 136, 43)) {
    font.setFont(u8g2_font_wqy16_t_gb2312);
    drawClipped(font, 18, 158, 122,
                music.title.isEmpty() ? "音乐播放器" : music.title, strong);
    font.setFont(u8g2_font_wqy12_t_gb2312);
    String byline = music.artist;
    if (!music.album.isEmpty()) byline += (byline.isEmpty() ? "" : " · ") + music.album;
    drawClipped(font, 18, 179, 122, byline, medium);
  }

  constexpr int16_t lyricLeft = 158;
  constexpr int16_t lyricWidth = 150;
  if (regionVisible(regions, 151, 22, 164, 160)) {
    int64_t elapsed = music.lineStartMs >= 0 ? positionMs - music.lineStartMs : 500;
    auto staggeredEase = [elapsed](int delayMs) {
      const float t = constrain(static_cast<float>(elapsed - delayMs) / 500.0f,
                                0.0f, 1.0f);
      const float remaining = 1.0f - t;
      return 1.0f - remaining * remaining * remaining;
    };
    const float previousEase = staggeredEase(0);
    const float currentEase = staggeredEase(50);
    const float nextEase = staggeredEase(100);
    const int16_t previousBaseline = 55 + static_cast<int16_t>((1.0f - previousEase) * 43.0f);
    const int16_t currentBaseline = 98 + static_cast<int16_t>((1.0f - currentEase) * 43.0f);
    const int16_t nextBaseline = 145 + static_cast<int16_t>((1.0f - nextEase) * 35.0f);

    font.setFont(u8g2_font_wqy12_t_gb2312);
    drawClipped(font, lyricLeft, previousBaseline, lyricWidth,
                music.previousLyric, alphaBlend(backdrop, kText,
                    static_cast<uint8_t>(0x42 + previousEase * 0x24)));

    const String lyric = music.lyric.isEmpty() ? "暂无歌词" : music.lyric;
    font.setFont(u8g2_font_wqy16_t_gb2312);
    const uint16_t activeLine = alphaBlend(medium, strong,
        static_cast<uint8_t>(currentEase * 255.0f));
    if (music.interlude && music.lineStartMs >= 0 && music.lineDurationMs > 0) {
      // Refined divides the gap into three equal word-like animations. The
      // active row fades in after its line transition and breathes as a group.
      const int64_t local = min<int64_t>(music.lineDurationMs,
                                         max<int64_t>(0, positionMs - music.lineStartMs));
      const int64_t perDot = max<int64_t>(1, music.lineDurationMs / 3);
      const float breath = music.playing
          ? 1.0f + 0.05f * sinf(static_cast<float>(now % 2000UL) * PI / 1000.0f)
          : 1.0f;
      for (int index = 0; index < 3; ++index) {
        const float progress = constrain(
            static_cast<float>(local - perDot * index) / static_cast<float>(perDot),
            0.0f, 1.0f);
        const float scale = (0.9f + 0.1f * min(1.0f, progress * 2.0f)) * breath;
        const uint8_t opacity = static_cast<uint8_t>(
            (0.2f + 0.7f * progress) * currentEase * 255.0f);
        const int16_t radius = max<int16_t>(3, static_cast<int16_t>(4.0f * scale + 0.5f));
        display.fillCircle(lyricLeft + 7 + index * 15, currentBaseline - 6, radius,
                           alphaBlend(backdrop, strong, opacity));
      }
    } else if (music.highlightedLyric.isEmpty() && music.currentWord.isEmpty()) {
      drawTimedScrollingLine(font, lyricLeft, currentBaseline, lyricWidth, lyric,
                             positionMs, music.lineStartMs,
                             music.lineDurationMs, activeLine);
    } else {
      drawKaraokeLine(font, lyricLeft, currentBaseline, lyricWidth, lyric,
                      music.highlightedLyric, music.currentWord,
                      music.wordProgressAt(now), medium, strong);
    }
    font.setFont(u8g2_font_wqy12_t_gb2312);
    if (!music.translatedLyric.isEmpty()) {
      drawTimedScrollingLine(font, lyricLeft, currentBaseline + 22, lyricWidth,
                             music.translatedLyric, positionMs,
                             music.lineStartMs, music.lineDurationMs, medium);
    }
    drawClipped(font, lyricLeft, nextBaseline, lyricWidth, music.nextLyric,
                alphaBlend(backdrop, kText,
                    static_cast<uint8_t>(0x2E + nextEase * 0x20)));
  }

  if (regionVisible(regions, 0, 184, 320, 56)) {
    display.fillRect(0, 187, display.width(), 53, alphaBlend(kCanvas, albumTone, 0x38));
    constexpr int16_t progressLeft = 15;
    constexpr int16_t progressWidth = 290;
    display.fillRoundRect(progressLeft, 190, progressWidth, 3, 1, faint);
    if (music.durationMs > 0) {
      const int64_t bounded = min<int64_t>(max<int64_t>(0, positionMs), music.durationMs);
      const int16_t filled = static_cast<int16_t>(bounded * progressWidth / music.durationMs);
      if (filled > 0) display.fillRoundRect(progressLeft, 190, filled, 3, 1, accent);
    }
    drawUtf8(font, 15, 210, formatTime(positionMs), medium);
    const String duration = formatTime(music.durationMs);
    drawUtf8(font, 305 - textWidth(font, duration), 210, duration, medium);

    const uint16_t previousColor = pressedControl == MediaControlCommand::Previous
        ? strong : medium;
    const uint16_t nextColor = pressedControl == MediaControlCommand::Next
        ? strong : medium;
    if (pressedControl == MediaControlCommand::Previous) {
      display.fillCircle(119, 218, 17, alphaBlend(kCanvas, accent, 0x70));
    }
    if (pressedControl == MediaControlCommand::Next) {
      display.fillCircle(201, 218, 17, alphaBlend(kCanvas, accent, 0x70));
    }
    display.fillTriangle(115, 218, 123, 212, 123, 224, previousColor);
    const uint16_t playSurface = pressedControl == MediaControlCommand::PlayPause
        ? strong : accent;
    display.fillCircle(160, 218, 17, playSurface);
    if (music.playing) {
      display.fillRect(154, 211, 4, 14, kCanvas);
      display.fillRect(162, 211, 4, 14, kCanvas);
    } else {
      display.fillTriangle(156, 210, 156, 226, 168, 218, kCanvas);
    }
    display.fillTriangle(197, 212, 197, 224, 205, 218, nextColor);
  }
  font.setFont(u8g2_font_wqy12_t_gb2312);
}

void TftFrameRenderer::renderMusicPipWindow(Adafruit_GFX& display,
                                            U8G2_FOR_ADAFRUIT_GFX& font,
                                            const MusicState& music,
                                            const TftRenderRegions* regions) {
  // PiPWindow's visual language: album-derived blurred wash outside a floating
  // 2:1 information card, a compact cover at the leading edge, and lyrics that
  // diminish line by line instead of a dedicated full-screen lyric stage.
  const uint16_t fallbackTone = musicAccent(music.songId);
  const uint16_t albumTone = AlbumArtCache::instance().dominantColor(fallbackTone);
  const uint16_t outside = alphaBlend(kCanvas, albumTone, 0x44);
  const uint16_t panel = alphaBlend(albumTone, kText, 0x68);
  const uint16_t panelStroke = alphaBlend(albumTone, kCanvas, 0x54);
  const uint16_t textStrong = alphaBlend(kText, albumTone, 0x18);
  const uint16_t textMedium = alphaBlend(kText, albumTone, 0x68);
  const uint16_t textFaint = alphaBlend(kText, albumTone, 0xA8);
  const unsigned long now = millis();
  const int64_t positionMs = music.positionAt(now);

  if (!drawAlbumBackdrop(display, regions, 255, kCanvas)) {
    fillRenderRegions(display, outside, regions);
  }
  // The card intentionally stands apart from the rest of the firmware UI.
  // It mirrors the reference plugin's floating window rather than our shell.
  display.fillRoundRect(9, 13, 302, 217, 13, panel);
  display.drawRoundRect(9, 13, 302, 217, 13, panelStroke);

  constexpr int16_t coverLeft = 18;
  constexpr int16_t coverTop = 23;
  constexpr int16_t coverSize = 80;
  if (regionVisible(regions, coverLeft, coverTop, coverSize + 2, coverSize + 2)) {
  if (!AlbumArtCache::instance().draw(display, coverLeft, coverTop, coverSize)) {
    display.fillRoundRect(coverLeft, coverTop, coverSize, coverSize, 8,
                          alphaBlend(panel, albumTone, 0x42));
    display.drawCircle(coverLeft + coverSize / 2, coverTop + coverSize / 2, 26,
                       alphaBlend(textStrong, panel, 0x5C));
    display.fillCircle(coverLeft + coverSize / 2, coverTop + coverSize / 2, 5, textStrong);
  }
  display.drawRoundRect(coverLeft, coverTop, coverSize, coverSize, 8,
                        alphaBlend(panelStroke, kText, 0x72));
  }

  constexpr int16_t infoLeft = 111;
  constexpr int16_t infoWidth = 184;
  if (regionVisible(regions, 102, 20, 194, 70)) {
  font.setFont(u8g2_font_wqy16_t_gb2312);
  drawClipped(font, infoLeft, 44, infoWidth,
              music.title.isEmpty() ? "音乐播放器" : music.title, textStrong);
  font.setFont(u8g2_font_wqy12_t_gb2312);
  drawClipped(font, infoLeft, 64, infoWidth,
              music.album.isEmpty() ? music.artist : music.album, textMedium);
  drawClipped(font, infoLeft, 83, infoWidth, music.artist, textMedium);
  }

  if (regionVisible(regions, 14, 101, 284, 29)) {
  const String time = formatTime(positionMs) + " / " + formatTime(music.durationMs);
  drawUtf8(font, 18, 116, time, textMedium);
  constexpr int16_t progressLeft = 102;
  constexpr int16_t progressTop = 105;
  constexpr int16_t progressWidth = 193;
  display.fillRect(progressLeft, progressTop, progressWidth, 2, textFaint);
  if (music.durationMs > 0) {
    const int64_t bounded = min<int64_t>(max<int64_t>(0, positionMs), music.durationMs);
    const int16_t filled = static_cast<int16_t>(bounded * progressWidth / music.durationMs);
    if (filled > 0) display.fillRect(progressLeft, progressTop, filled, 2, textStrong);
  }
  display.drawFastHLine(18, 127, 277, alphaBlend(panelStroke, kCanvas, 0x60));
  }

  if (regionVisible(regions, 14, 132, 284, 94)) {
  const String lyric = music.lyric.isEmpty() ? "暂无歌词" : music.lyric;
  font.setFont(u8g2_font_wqy16_t_gb2312);
  if (music.highlightedLyric.isEmpty() && music.currentWord.isEmpty()) {
    drawTimedScrollingLine(font, 18, 155, 276, lyric, positionMs, music.lineStartMs,
                           music.lineDurationMs, textStrong);
  } else {
    drawKaraokeLine(font, 18, 155, 276, lyric, music.highlightedLyric, music.currentWord,
                    music.wordProgressAt(now), textMedium, textStrong);
  }
  font.setFont(u8g2_font_wqy12_t_gb2312);
  if (!music.translatedLyric.isEmpty()) {
    drawTimedScrollingLine(font, 18, 174, 276, music.translatedLyric, positionMs,
                           music.lineStartMs, music.lineDurationMs, textMedium);
  }
  font.setFont(u8g2_font_wqy16_t_gb2312);
  drawClipped(font, 18, 202, 276, music.nextLyric, textMedium);
  font.setFont(u8g2_font_wqy12_t_gb2312);
  drawClipped(font, 18, 220, 276, music.previousLyric, textFaint);
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
                                         const NavState& state,
                                         const TftRenderRegions* regions) {
  drawShell(display);

  // layout_floating_navi_normal.xml: a 60dp exit tile within a 100dp header.
  if (regionVisible(regions, 7, 5, 210, 90)) {
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
  }

  if (regionVisible(regions, 205, 3, 115, 116)) {
  if (state.lightCount > 0) {
    drawNavigationTrafficPill(display, state);
  } else if (state.camera.distance >= 0) {
    drawCameraPill(display, font, state, 214, 5, 58);
  }
  drawSpeedLimitSign(display, state, 294, 30);
  }

  int16_t nextTop = 100;
  if (state.lane.count > 0) {
    if (regionVisible(regions, 7, 94, 306, 70)) {
    drawLanes(display, state, nextTop);
    }
    nextTop += 48;
  }
  if (regionVisible(regions, 7, 96, 306, 75)) {
  drawTmc(display, state, 10, nextTop + 2, display.width() - 20);
  }
  if (regionVisible(regions, 7, 116, 306, 120)) {
  drawNavigationInfo(display, font, state, nextTop + 18);
  }
}

void TftFrameRenderer::renderCruise(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& font,
                                    const NavState& state,
                                    const TftRenderRegions* regions) {
  drawShell(display);

  // layout_floating_cruise_normal.xml: 40sp speed, road name, then optional
  // traffic-light row and the native lane-line strip.
  if (regionVisible(regions, 7, 5, 205, 52)) {
  drawBig(display, 10, 10, state.speed.current >= 0 ? String(state.speed.current) : "--", 5,
          kAccent);
  display.fillRect(76, 14, 1, 32, alphaBlend(kSurface, kText, 0x33));
  drawClipped(font, 87, 39, 120, state.road, kText);
  }

  int16_t laneTop = 60;
  if (state.lightCount > 0) {
    if (regionVisible(regions, 205, 3, 115, 116)) {
    drawCruiseTrafficPills(display, state, 58);
    }
    laneTop = 114;
  } else if (state.camera.distance >= 0) {
    if (regionVisible(regions, 205, 3, 115, 116)) {
    drawCameraPill(display, font, state, 211, 8, 61);
    }
  }
  if (regionVisible(regions, 205, 3, 115, 116)) {
  drawSpeedLimitSign(display, state, 294, 33);
  }
  if (regionVisible(regions, 7, 54, 306, 128)) {
  drawLanes(display, state, laneTop);
  }
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

  const int currentWordOffset = currentWord.isEmpty()
      ? -1 : visibleHighlight.lastIndexOf(currentWord);
  String trailingHighlight = currentWordOffset < 0
      ? String("x")
      : visibleHighlight.substring(currentWordOffset + currentWord.length());
  trailingHighlight.trim();
  const bool currentWordAligned = currentWordOffset >= 0 && trailingHighlight.isEmpty();
  if (!currentWordAligned) {
    drawClipped(font, x, baseline, maxWidth, visibleHighlight, activeColor);
    return;
  }

  const String completed = visibleHighlight.substring(0, currentWordOffset);
  drawClipped(font, x, baseline, maxWidth, completed, activeColor);
  const int16_t wordX = x + textWidth(font, completed);
  const int16_t wordWidth = textWidth(font, currentWord);
  if (wordProgressPermille <= 0) return;
  // Keep fallback glyphs transparent while still animating every YRC word.
  // Blending the glyph color at the display's 25 FPS cadence avoids the hard
  // whole-word flash and the opaque rectangles produced by bitmap clipping.
  const int progress = constrain(wordProgressPermille, 0, 1000);
  const int eased = progress * progress * (3000 - 2 * progress) / 1000000;
  const uint16_t currentColor = alphaBlend(
      idleColor, activeColor, static_cast<uint8_t>(eased * 255 / 1000));
  drawClipped(font, wordX, baseline, wordWidth, currentWord, currentColor);
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
  size_t start = 0;
  int16_t skipped = 0;
  while (start < text.length()) {
    uint16_t codepoint = 0;
    size_t bytes = 1;
    if (!decodeUtf8(text, start, codepoint, bytes)) break;
    const int16_t glyph = glyphWidth(font, primary, codepoint);
    if (skipped + glyph > offset) break;
    skipped += glyph;
    start += bytes;
  }
  font.setFont(primary);
  // Do not append an ellipsis to a moving viewport: at the tail that suffix
  // displaced the real final word forever. At the terminal position advance
  // to the earliest UTF-8 suffix that fits, guaranteeing the last word is
  // visible before the next lyric transition.
  if (scrollProgress >= 1000) {
    while (start < text.length() && textWidth(font, text.substring(start)) > maxWidth) {
      start += utf8CharacterBytes(static_cast<uint8_t>(text[start]));
    }
  }
  drawUtf8(font, x, baseline, clipUtf8Window(font, text.substring(start), maxWidth), color);
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

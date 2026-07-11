#include "TftPreviewRenderer.h"

#include <esp_heap_caps.h>

#include "Config.h"
#include "TftFrameRenderer.h"

namespace {
constexpr size_t kPixels = AMAP_TFT_WIDTH * AMAP_TFT_HEIGHT;
constexpr size_t kPixelBytes = kPixels * sizeof(uint16_t);
constexpr size_t kBmpHeaderBytes = 66;

void writeU16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}
}  // namespace

TftPreviewRenderer::Canvas::Canvas() : Adafruit_GFX(AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT) {}

TftPreviewRenderer::Canvas::~Canvas() {
  free(buffer);
}

bool TftPreviewRenderer::Canvas::begin() {
  if (buffer != nullptr) {
    return true;
  }
  buffer = static_cast<uint16_t*>(
      heap_caps_malloc(kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<uint16_t*>(malloc(kPixelBytes));
  }
  return buffer != nullptr;
}

bool TftPreviewRenderer::Canvas::isReady() const {
  return buffer != nullptr;
}

const uint16_t* TftPreviewRenderer::Canvas::pixels() const {
  return buffer;
}

void TftPreviewRenderer::Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (buffer == nullptr || x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) {
    return;
  }
  buffer[y * WIDTH + x] = color;
}

void TftPreviewRenderer::Canvas::drawFastHLine(int16_t x, int16_t y, int16_t width,
                                                uint16_t color) {
  if (buffer == nullptr || y < 0 || y >= HEIGHT || width <= 0) {
    return;
  }
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (x + width > WIDTH) {
    width = WIDTH - x;
  }
  for (int16_t column = 0; column < width; ++column) {
    buffer[y * WIDTH + x + column] = color;
  }
}

void TftPreviewRenderer::Canvas::drawFastVLine(int16_t x, int16_t y, int16_t height,
                                                uint16_t color) {
  if (buffer == nullptr || x < 0 || x >= WIDTH || height <= 0) {
    return;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (y + height > HEIGHT) {
    height = HEIGHT - y;
  }
  for (int16_t row = 0; row < height; ++row) {
    buffer[(y + row) * WIDTH + x] = color;
  }
}

void TftPreviewRenderer::Canvas::fillScreen(uint16_t color) {
  if (buffer == nullptr) {
    return;
  }
  for (size_t index = 0; index < kPixels; ++index) {
    buffer[index] = color;
  }
}

bool TftPreviewRenderer::sendBmp(WebServer& server, const NavState& state, bool wifiConnected,
                                 unsigned long silenceMs) {
  if (!canvas.begin()) {
    return false;
  }
  if (!fontReady) {
    font.begin(canvas);
    font.setFontMode(1);
    font.setFont(u8g2_font_wqy12_t_gb2312);
    fontReady = true;
  }

  TftFrameRenderer::render(canvas, font, state, wifiConnected, silenceMs);

  uint8_t header[kBmpHeaderBytes] = {};
  header[0] = 'B';
  header[1] = 'M';
  writeU32(header + 2, kBmpHeaderBytes + kPixelBytes);
  writeU32(header + 10, kBmpHeaderBytes);
  writeU32(header + 14, 40);
  writeU32(header + 18, AMAP_TFT_WIDTH);
  writeU32(header + 22, static_cast<uint32_t>(-AMAP_TFT_HEIGHT));
  writeU16(header + 26, 1);
  writeU16(header + 28, 16);
  writeU32(header + 30, 3);
  writeU32(header + 34, kPixelBytes);
  writeU32(header + 38, 2835);
  writeU32(header + 42, 2835);
  writeU32(header + 54, 0xF800);
  writeU32(header + 58, 0x07E0);
  writeU32(header + 62, 0x001F);

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(kBmpHeaderBytes + kPixelBytes);
  server.send(200, "image/bmp", "");
  WiFiClient client = server.client();
  if (client.write(header, sizeof(header)) != sizeof(header)) {
    return false;
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(canvas.pixels());
  for (size_t row = 0; row < AMAP_TFT_HEIGHT; ++row) {
    const size_t offset = row * AMAP_TFT_WIDTH * sizeof(uint16_t);
    if (client.write(bytes + offset, AMAP_TFT_WIDTH * sizeof(uint16_t)) !=
        AMAP_TFT_WIDTH * sizeof(uint16_t)) {
      return false;
    }
  }
  return true;
}

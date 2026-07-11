#include "TftRenderer.h"

#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "Config.h"
#include "TftFrameRenderer.h"

namespace {
Adafruit_ST7789 tft(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
U8G2_FOR_ADAFRUIT_GFX tftFont;
}  // namespace

void TftRenderer::begin() {
  if (AMAP_TFT_SCLK_PIN < 0 || AMAP_TFT_MOSI_PIN < 0 || AMAP_TFT_CS_PIN < 0 ||
      AMAP_TFT_DC_PIN < 0 || AMAP_TFT_RST_PIN < 0 || AMAP_TFT_BL_PIN < 0) {
    Serial.println("TFT disabled: ST7789 pins are not configured");
    return;
  }

  pinMode(AMAP_TFT_BL_PIN, OUTPUT);
  digitalWrite(AMAP_TFT_BL_PIN, LOW);
  SPI.begin(AMAP_TFT_SCLK_PIN, AMAP_TFT_MISO_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
  tft.init(240, 320, SPI_MODE0);
  tft.setRotation(AMAP_TFT_ROTATION);
  tft.setSPISpeed(40000000);
  tft.fillScreen(0x0861);
  tftFont.begin(tft);
  tftFont.setFontMode(1);
  tftFont.setFont(u8g2_font_wqy12_t_gb2312);
  digitalWrite(AMAP_TFT_BL_PIN, HIGH);
  ready = true;
  Serial.printf("ST7789 ready: %dx%d, SPI SCK=%d MOSI=%d CS=%d\n", tft.width(), tft.height(),
                AMAP_TFT_SCLK_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
}

bool TftRenderer::isReady() const {
  return ready;
}

void TftRenderer::render(const NavState& state, bool wifiConnected, const String& ip,
                         uint16_t port, unsigned long silenceMs) {
  if (!ready) {
    return;
  }
  TftFrameRenderer::render(tft, tftFont, state, wifiConnected, silenceMs);
  (void)ip;
  (void)port;
}

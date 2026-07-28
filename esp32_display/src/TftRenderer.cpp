#include "TftRenderer.h"

#include "Config.h"

#include <Adafruit_ILI9341.h>
#include <Adafruit_SPITFT.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/semphr.h>

#include "HardwareSettings.h"
#include "AlbumArtCache.h"
#include "TftFrameRenderer.h"

namespace {
Adafruit_ST7789 st7789(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
Adafruit_ILI9341 ili9341(AMAP_TFT_CS_PIN, AMAP_TFT_DC_PIN, AMAP_TFT_RST_PIN);
Adafruit_SPITFT* activePanel = nullptr;
esp_lcd_panel_handle_t dmaPanel = nullptr;
esp_lcd_panel_io_handle_t dmaPanelIo = nullptr;
SemaphoreHandle_t dmaTransferDone = nullptr;
U8G2_FOR_ADAFRUIT_GFX tftFont;
U8G2_FOR_ADAFRUIT_GFX adjacentFont;

constexpr size_t kPixels = AMAP_TFT_WIDTH * AMAP_TFT_HEIGHT;
constexpr size_t kPixelBytes = kPixels * sizeof(uint16_t);
constexpr size_t kTransferPixels = 8U * 1024U;  // 16 KB internal DMA-capable staging
constexpr size_t kTransferBytes = kTransferPixels * sizeof(uint16_t);
constexpr int16_t kDirtyTileWidth = 16;
constexpr int16_t kDirtyTileHeight = 8;
constexpr int16_t kDirtyTileColumns = AMAP_TFT_WIDTH / kDirtyTileWidth;
constexpr int16_t kDirtyTileRows = AMAP_TFT_HEIGHT / kDirtyTileHeight;
constexpr size_t kDirtyTileCount = kDirtyTileColumns * kDirtyTileRows;
constexpr size_t kFullRefreshDirtyTiles = kDirtyTileCount * 45 / 100;
constexpr size_t kMaxDirtyRectangles = 96;

static_assert(AMAP_TFT_WIDTH % kDirtyTileWidth == 0,
              "TFT width must be divisible by dirty tile width");
static_assert(AMAP_TFT_HEIGHT % kDirtyTileHeight == 0,
              "TFT height must be divisible by dirty tile height");

struct DirtyRectangle {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

const char* renderedViewName(TftViewMode view) {
  switch (view) {
    case TftViewMode::Home: return "home";
    case TftViewMode::Navigation: return "navigation";
    case TftViewMode::Music: return "music";
    case TftViewMode::Weather: return "weather";
    case TftViewMode::Settings: return "settings";
    case TftViewMode::AutoStatus: return "auto_status";
    default: return "auto";
  }
}

bool IRAM_ATTR onDmaColorTransferDone(esp_lcd_panel_io_handle_t,
                                     esp_lcd_panel_io_event_data_t*, void* userContext) {
  BaseType_t taskWoken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(userContext), &taskWoken);
  return taskWoken == pdTRUE;
}

bool beginSt7789DmaPanel(bool invertColors) {
  dmaTransferDone = xSemaphoreCreateBinary();
  if (dmaTransferDone == nullptr) return false;

  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = AMAP_TFT_MOSI_PIN;
  busConfig.miso_io_num = -1;
  busConfig.sclk_io_num = AMAP_TFT_SCLK_PIN;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = kTransferBytes;
  esp_err_t result = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) {
    Serial.printf("TFT DMA bus init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  esp_lcd_panel_io_spi_config_t ioConfig = {};
  ioConfig.cs_gpio_num = AMAP_TFT_CS_PIN;
  ioConfig.dc_gpio_num = AMAP_TFT_DC_PIN;
  ioConfig.spi_mode = 0;
  ioConfig.pclk_hz = AMAP_TFT_SPI_FREQUENCY;
  ioConfig.trans_queue_depth = 1;
  ioConfig.on_color_trans_done = onDmaColorTransferDone;
  ioConfig.user_ctx = dmaTransferDone;
  ioConfig.lcd_cmd_bits = 8;
  ioConfig.lcd_param_bits = 8;
  result = esp_lcd_new_panel_io_spi(
      reinterpret_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &ioConfig, &dmaPanelIo);
  if (result != ESP_OK) {
    Serial.printf("TFT DMA IO init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  esp_lcd_panel_dev_config_t panelConfig = {};
  panelConfig.reset_gpio_num = AMAP_TFT_RST_PIN;
  panelConfig.color_space = ESP_LCD_COLOR_SPACE_RGB;
  panelConfig.bits_per_pixel = 16;
  result = esp_lcd_new_panel_st7789(dmaPanelIo, &panelConfig, &dmaPanel);
  if (result != ESP_OK || esp_lcd_panel_reset(dmaPanel) != ESP_OK ||
      esp_lcd_panel_init(dmaPanel) != ESP_OK ||
      esp_lcd_panel_swap_xy(dmaPanel, true) != ESP_OK ||
      esp_lcd_panel_mirror(dmaPanel, false, true) != ESP_OK ||
      esp_lcd_panel_invert_color(dmaPanel, invertColors) != ESP_OK ||
      esp_lcd_panel_disp_on_off(dmaPanel, true) != ESP_OK) {
    Serial.printf("TFT DMA panel init failed: %s\n", esp_err_to_name(result));
    dmaPanel = nullptr;
    return false;
  }
  return true;
}

void hashBytes(uint32_t& hash, const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
}

template <typename T>
void hashValue(uint32_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(value));
}

void hashString(uint32_t& hash, const String& value) {
  hashBytes(hash, value.c_str(), value.length());
  const uint8_t separator = 0xFF;
  hashValue(hash, separator);
}

void hashMusic(uint32_t& hash, const MusicState& music, bool includePosition,
               unsigned long now) {
  hashValue(hash, music.active);
  hashValue(hash, music.playing);
  hashString(hash, music.source);
  hashString(hash, music.sourceName);
  hashValue(hash, music.songId);
  hashString(hash, music.title);
  hashString(hash, music.artist);
  hashString(hash, music.album);
  hashString(hash, music.coverUrl);
  if (includePosition) {
    // Progress and lyric motion are intentionally capped at 25 FPS. Static
    // metadata changes still invalidate immediately, while a 60 Hz render
    // loop no longer lays out the same lyric frame more than once.
    const int64_t positionFrame = music.positionAt(now) / 40;
    hashValue(hash, positionFrame);
  }
  hashValue(hash, music.durationMs);
  hashString(hash, music.previousLyric);
  hashString(hash, music.lyric);
  hashString(hash, music.translatedLyric);
  hashString(hash, music.nextLyric);
  hashString(hash, music.highlightedLyric);
  hashString(hash, music.currentWord);
  hashValue(hash, music.lineStartMs);
  hashValue(hash, music.lineDurationMs);
  hashValue(hash, music.wordStartMs);
  hashValue(hash, music.wordDurationMs);
}

void hashWeather(uint32_t& hash, const WeatherState& weather) {
  hashValue(hash, weather.configured);
  hashValue(hash, weather.loading);
  hashValue(hash, weather.valid);
  hashString(hash, weather.city);
  hashString(hash, weather.timezone);
  hashString(hash, weather.condition);
  hashString(hash, weather.error);
  hashValue(hash, weather.temperatureC);
  hashValue(hash, weather.feelsLikeC);
  hashValue(hash, weather.windKph);
  hashValue(hash, weather.humidity);
  hashValue(hash, weather.weatherCode);
  hashValue(hash, weather.airQualityValid);
  hashValue(hash, weather.usAqi);
  hashValue(hash, weather.pm25);
  hashValue(hash, weather.pm10);
  hashString(hash, weather.airQualityError);
  hashValue(hash, weather.isDay);
  for (const WeatherForecastDay& day : weather.days) {
    hashString(hash, day.date);
    hashValue(hash, day.weatherCode);
    hashValue(hash, day.highC);
    hashValue(hash, day.lowC);
    hashValue(hash, day.rainChance);
  }
}

void hashPhone(uint32_t& hash, const PhoneState& phone) {
  hashValue(hash, phone.enabled);
  hashValue(hash, phone.notification.active);
  hashString(hash, phone.notification.kind);
  hashString(hash, phone.notification.app);
  hashString(hash, phone.notification.sender);
  hashString(hash, phone.notification.title);
  hashString(hash, phone.notification.body);
  hashValue(hash, phone.weather.temperatureC);
  hashString(hash, phone.weather.condition);
  hashValue(hash, phone.weather.aqi);
  hashString(hash, phone.weather.alert);
  hashString(hash, phone.calendar.title);
  hashString(hash, phone.calendar.location);
  hashValue(hash, phone.device.batteryPercent);
  hashValue(hash, phone.device.charging);
  hashString(hash, phone.device.network);
  hashValue(hash, phone.device.signalLevel);
}

uint32_t phoneSheetSignature(const PhoneState& phone, bool wifiConnected,
                             bool bleConnected, bool detail, uint8_t detailScroll) {
  uint32_t hash = 2166136261UL;
  hashValue(hash, wifiConnected);
  hashValue(hash, bleConnected);
  hashValue(hash, detail);
  hashValue(hash, detailScroll);
  hashPhone(hash, phone);
  return hash;
}

void hashNavigation(uint32_t& hash, const NavState& state) {
  hashString(hash, state.mode);
  hashString(hash, state.road);
  hashValue(hash, state.turn.icon);
  hashString(hash, state.turn.distanceText);
  hashString(hash, state.turn.road);
  hashString(hash, state.eta.remainDistanceText);
  hashString(hash, state.eta.remainTimeText);
  hashString(hash, state.eta.arriveTimeText);
  hashValue(hash, state.speed.current);
  hashValue(hash, state.speed.limit);
  hashValue(hash, state.speed.overspeedLevel);
  hashValue(hash, state.lane.count);
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    hashValue(hash, state.lane.lanes[i]);
    hashValue(hash, state.lane.advised[i]);
  }
  hashValue(hash, state.lightCount);
  for (uint8_t i = 0; i < state.lightCount; ++i) {
    hashValue(hash, state.lights[i].dir);
    hashValue(hash, state.lights[i].status);
    hashValue(hash, state.lights[i].seconds);
  }
  hashValue(hash, state.camera.type);
  hashValue(hash, state.camera.distance);
  hashValue(hash, state.camera.speedLimit);
  hashValue(hash, state.tmc.totalDistance);
  hashValue(hash, state.tmc.finishDistance);
  hashValue(hash, state.tmc.count);
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    hashValue(hash, state.tmc.status[i]);
    hashValue(hash, state.tmc.distance[i]);
  }
  hashValue(hash, state.route.remainingMeters);
  hashValue(hash, state.route.remainingSeconds);
  hashValue(hash, state.route.progressPercent);
  hashString(hash, state.route.destination);
  hashString(hash, state.guide.exitName);
  hashString(hash, state.guide.exitDirection);
  hashString(hash, state.guide.serviceAreaName);
  hashString(hash, state.guide.serviceAreaDistance);
  hashString(hash, state.guide.nextServiceAreaName);
  hashString(hash, state.guide.nextServiceAreaDistance);
  hashString(hash, state.alert);
  hashString(hash, state.detail);
}

uint32_t frameSignature(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                          unsigned long now, TftViewMode viewMode,
                          int16_t dragOffsetX, bool showGestureHint,
                          MediaControlCommand pressedControl, int8_t pressedSettingsRow,
                          bool phoneDetail, uint8_t phoneDetailScroll,
                          bool phoneSheetVisible, int16_t phoneSheetOffsetY,
                          bool automaticMode, bool homeTransitionVisible,
                          int16_t homeTransitionOffsetY, uint8_t settingsPage,
                          int16_t homeScroll,
                          bool weatherRetryPressed,
                          const WeatherState& weather, MusicPageStyle musicPageStyle,
                          const DisplayPreferences& preferences) {
  uint32_t hash = 2166136261UL;
  const uint8_t view = static_cast<uint8_t>(viewMode);
  hashValue(hash, view);
  hashValue(hash, showGestureHint);
  hashValue(hash, phoneSheetVisible);
  hashValue(hash, homeTransitionVisible);

  // Transition source/destination frames are snapshots. While either is
  // moving, only its presentation offset can change the visible frame.
  if (homeTransitionVisible) {
    hashValue(hash, homeTransitionOffsetY);
    return hash;
  }
  if (phoneSheetVisible) {
    hashValue(hash, phoneSheetOffsetY);
    hashValue(hash, phoneDetail);
    hashValue(hash, phoneDetailScroll);
    hashValue(hash, wifiConnected);
    hashValue(hash, bleConnected);
    hashPhone(hash, state.phone);
    return hash;
  }

  if (dragOffsetX != 0) hashValue(hash, dragOffsetX);
  const bool connected = wifiConnected || bleConnected;
  switch (viewMode) {
    case TftViewMode::Home:
      hashValue(hash, homeScroll);
      hashValue(hash, automaticMode);
      hashValue(hash, connected);
      hashValue(hash, state.active);
      hashString(hash, state.turn.distanceText);
      hashValue(hash, state.music.active);
      hashString(hash, state.music.title);
      // Home shows only the final compact weather summary, never forecasts,
      // AQI, update timestamps, or background request errors.
      hashValue(hash, weather.configured);
      hashValue(hash, weather.loading);
      hashValue(hash, weather.valid);
      hashValue(hash, weather.temperatureC);
      hashString(hash, weather.condition);
      break;
    case TftViewMode::Music: {
      const uint8_t availability = !connected ? 0
          : silenceMs > AMAP_STANDBY_MS ? 1
          : state.music.active ? 2 : 1;
      hashValue(hash, availability);
      if (availability == 1) {
        hashValue(hash, wifiConnected);
        hashValue(hash, bleConnected);
        hashString(hash, ip);
        hashValue(hash, port);
      } else if (availability == 2) {
        const uint8_t pressed = static_cast<uint8_t>(pressedControl);
        hashValue(hash, pressed);
        hashValue(hash, musicPageStyle);
        hashMusic(hash, state.music, true, now);
        hashValue(hash, AlbumArtCache::instance().revision());
      }
      break;
    }
    case TftViewMode::Navigation: {
      const uint8_t availability = !connected ? 0
          : silenceMs > AMAP_STANDBY_MS ? 1
          : state.active ? 2 : 1;
      hashValue(hash, availability);
      if (availability == 2) {
        hashNavigation(hash, state);
      } else {
        hashValue(hash, wifiConnected);
        hashValue(hash, bleConnected);
        hashString(hash, ip);
        hashValue(hash, port);
      }
      break;
    }
    case TftViewMode::Weather:
      hashValue(hash, wifiConnected);
      hashValue(hash, weatherRetryPressed);
      hashWeather(hash, weather);
      break;
    case TftViewMode::AutoStatus:
      hashValue(hash, wifiConnected);
      hashValue(hash, bleConnected);
      hashString(hash, ip);
      hashValue(hash, port);
      hashValue(hash, automaticMode);
      hashValue(hash, pressedSettingsRow);
      hashValue(hash, state.active);
      hashValue(hash, state.music.active);
      break;
    case TftViewMode::Settings:
      hashValue(hash, settingsPage);
      hashValue(hash, pressedSettingsRow);
      hashValue(hash, wifiConnected);
      hashValue(hash, bleConnected);
      if (settingsPage == 3) {
        hashString(hash, ip);
        hashValue(hash, port);
      }
      hashValue(hash, preferences.brightness);
      hashValue(hash, preferences.nightDim);
      hashValue(hash, preferences.autoView);
      hashValue(hash, preferences.messageBanners);
      hashValue(hash, preferences.musicPageStyle);
      hashValue(hash, preferences.showFrameRate);
      break;
    default:
      // Auto is normally resolved before signature construction. Keep this
      // fallback page-scoped for preview and future callers.
      hashValue(hash, connected);
      hashValue(hash, state.active);
      hashValue(hash, state.music.active);
      break;
  }
  return hash;
}

void beginPageComponents(TftViewMode view, uint32_t components[8]) {
  for (uint8_t i = 0; i < 8; ++i) {
    components[i] = 2166136261UL;
    const uint8_t page = static_cast<uint8_t>(view);
    hashValue(components[i], page);
    hashValue(components[i], i);
  }
}

void buildPageComponents(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                         unsigned long now, TftViewMode view, bool automaticMode,
                         int16_t homeScroll, MediaControlCommand pressedControl,
                         int8_t pressedSettingsRow, uint8_t settingsPage,
                         bool weatherRetryPressed, const WeatherState& weather,
                         const DisplayPreferences& preferences,
                         uint32_t components[8]) {
  beginPageComponents(view, components);
  const bool connected = wifiConnected || bleConnected;
  switch (view) {
    case TftViewMode::Home:
      hashValue(components[0], homeScroll);
      hashValue(components[1], connected);
      hashValue(components[2], state.active);
      hashString(components[2], state.turn.distanceText);
      hashValue(components[3], state.music.active);
      hashString(components[3], state.music.title);
      hashValue(components[4], weather.configured);
      hashValue(components[4], weather.loading);
      hashValue(components[4], weather.valid);
      hashValue(components[4], weather.temperatureC);
      hashString(components[4], weather.condition);
      hashValue(components[5], automaticMode);
      break;
    case TftViewMode::Music: {
      const uint8_t availability = !connected ? 0
          : silenceMs > AMAP_STANDBY_MS ? 1
          : state.music.active ? 2 : 1;
      hashValue(components[0], availability);
      hashValue(components[0], preferences.musicPageStyle);
      if (availability != 2) {
        hashValue(components[0], wifiConnected);
        hashValue(components[0], bleConnected);
        hashString(components[0], ip);
        hashValue(components[0], port);
        break;
      }
      hashValue(components[0], state.music.songId);
      hashString(components[0], state.music.title);
      hashString(components[0], state.music.artist);
      hashString(components[0], state.music.album);
      hashString(components[0], state.music.sourceName);
      hashString(components[0], state.music.coverUrl);
      hashValue(components[0], AlbumArtCache::instance().revision());
      hashValue(components[0], state.music.durationMs);
      const int64_t positionFrame = state.music.positionAt(now) / 40;
      hashValue(components[1], positionFrame);
      hashValue(components[1], state.music.durationMs);
      hashString(components[2], state.music.previousLyric);
      hashString(components[2], state.music.lyric);
      hashString(components[2], state.music.translatedLyric);
      hashString(components[2], state.music.nextLyric);
      hashString(components[2], state.music.highlightedLyric);
      hashString(components[2], state.music.currentWord);
      hashValue(components[2], state.music.lineStartMs);
      hashValue(components[2], state.music.lineDurationMs);
      hashValue(components[2], state.music.wordStartMs);
      hashValue(components[2], state.music.wordDurationMs);
      hashValue(components[2], positionFrame);
      const uint8_t pressed = static_cast<uint8_t>(pressedControl);
      hashValue(components[3], state.music.playing);
      hashValue(components[3], pressed);
      break;
    }
    case TftViewMode::Navigation: {
      const uint8_t availability = !connected ? 0
          : silenceMs > AMAP_STANDBY_MS ? 1
          : state.active ? 2 : 1;
      hashValue(components[0], availability);
      if (availability != 2) {
        hashValue(components[0], wifiConnected);
        hashValue(components[0], bleConnected);
        hashString(components[0], ip);
        hashValue(components[0], port);
        break;
      }
      hashString(components[0], state.mode);
      hashValue(components[0], state.lane.count);
      hashValue(components[0], state.lightCount > 0);
      hashValue(components[0], state.camera.distance >= 0);
      hashValue(components[1], state.turn.icon);
      hashString(components[1], state.turn.distanceText);
      hashString(components[1], state.turn.road);
      hashString(components[1], state.road);
      hashValue(components[1], state.speed.current);
      hashValue(components[2], state.lightCount);
      for (uint8_t i = 0; i < state.lightCount; ++i) {
        hashValue(components[2], state.lights[i].dir);
        hashValue(components[2], state.lights[i].status);
        hashValue(components[2], state.lights[i].seconds);
      }
      hashValue(components[2], state.camera.type);
      hashValue(components[2], state.camera.distance);
      hashValue(components[2], state.camera.speedLimit);
      hashValue(components[2], state.speed.limit);
      hashValue(components[2], state.speed.overspeedLevel);
      hashValue(components[3], state.lane.count);
      for (uint8_t i = 0; i < state.lane.count; ++i) {
        hashValue(components[3], state.lane.lanes[i]);
        hashValue(components[3], state.lane.advised[i]);
      }
      hashValue(components[4], state.tmc.totalDistance);
      hashValue(components[4], state.tmc.finishDistance);
      hashValue(components[4], state.tmc.count);
      for (uint8_t i = 0; i < state.tmc.count; ++i) {
        hashValue(components[4], state.tmc.status[i]);
        hashValue(components[4], state.tmc.distance[i]);
      }
      hashString(components[5], state.eta.remainDistanceText);
      hashString(components[5], state.eta.remainTimeText);
      hashString(components[5], state.eta.arriveTimeText);
      hashValue(components[5], state.route.remainingMeters);
      hashValue(components[5], state.route.remainingSeconds);
      hashValue(components[5], state.route.progressPercent);
      hashString(components[5], state.route.destination);
      hashString(components[5], state.guide.exitName);
      hashString(components[5], state.guide.serviceAreaName);
      hashString(components[5], state.guide.serviceAreaDistance);
      hashString(components[5], state.road);
      break;
    }
    case TftViewMode::Weather:
      hashValue(components[0], weather.configured);
      hashValue(components[0], weather.valid);
      hashString(components[1], weather.city);
      hashValue(components[1], weather.loading);
      hashValue(components[2], wifiConnected);
      hashWeather(components[2], weather);
      hashValue(components[3], weatherRetryPressed);
      break;
    case TftViewMode::AutoStatus:
      hashValue(components[1], wifiConnected);
      hashValue(components[1], bleConnected);
      hashString(components[1], ip);
      hashValue(components[1], port);
      hashValue(components[2], state.active);
      hashValue(components[2], state.music.active);
      hashValue(components[3], automaticMode);
      hashValue(components[3], pressedSettingsRow);
      break;
    case TftViewMode::Settings:
      hashValue(components[0], settingsPage);
      hashValue(components[1], wifiConnected);
      hashValue(components[1], bleConnected);
      if (settingsPage == 3) {
        hashString(components[1], ip);
        hashValue(components[1], port);
      }
      hashValue(components[2], preferences.brightness);
      hashValue(components[2], preferences.nightDim);
      hashValue(components[2], preferences.autoView);
      hashValue(components[2], preferences.messageBanners);
      hashValue(components[2], preferences.musicPageStyle);
      hashValue(components[2], preferences.showFrameRate);
      hashValue(components[3], pressedSettingsRow);
      break;
    default:
      hashValue(components[0], connected);
      hashValue(components[0], state.active);
      hashValue(components[0], state.music.active);
      break;
  }
}

void addSettingsRowRegion(TftRenderRegions& regions, uint8_t settingsPage, int8_t row) {
  if (row < 0) return;
  if (settingsPage == 0) {
    regions.add(8, 54 + row * 38, 304, 41);
  } else {
    regions.add(8, 66 + row * 42, 304, 43);
  }
}

void buildChangedRegions(TftViewMode view, const uint32_t current[8],
                         const uint32_t previous[8], int16_t homeScroll,
                         MusicPageStyle musicPageStyle, uint8_t settingsPage,
                         int8_t pressedSettingsRow, int8_t previousPressedSettingsRow,
                         TftRenderRegions& regions) {
  if (current[0] != previous[0]) {
    regions.add(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT);
    return;
  }
  switch (view) {
    case TftViewMode::Home:
      if (current[1] != previous[1]) regions.add(278, 16, 20, 22);
      if (current[2] != previous[2]) regions.add(8, 51 - homeScroll, 150, 70);
      if (current[3] != previous[3]) regions.add(162, 51 - homeScroll, 150, 70);
      if (current[4] != previous[4]) regions.add(8, 124 - homeScroll, 150, 70);
      if (current[5] != previous[5]) regions.add(162, 124 - homeScroll, 150, 70);
      break;
    case TftViewMode::Music:
      if (musicPageStyle == MusicPageStyle::PipWindow) {
        if (current[1] != previous[1]) regions.add(14, 101, 284, 21);
        if (current[2] != previous[2]) regions.add(14, 132, 284, 94);
        if (current[3] != previous[3]) regions.add(102, 20, 194, 70);
      } else if (musicPageStyle == MusicPageStyle::RefinedNowPlaying) {
        if (current[1] != previous[1]) regions.add(0, 184, 320, 56);
        if (current[2] != previous[2]) regions.add(151, 22, 164, 160);
        if (current[3] != previous[3]) regions.add(0, 184, 320, 56);
      } else {
        if (current[1] != previous[1]) regions.add(10, 184, 136, 55);
        if (current[2] != previous[2]) regions.add(158, 39, 157, 121);
        // The transport buttons share boundary tiles with the progress bar
        // and both time labels.  Restore the complete control band for a
        // press/release or play-state change so an edge tile can never carry
        // pixels from an older rotated framebuffer.
        if (current[3] != previous[3]) regions.add(0, 184, 160, 56);
      }
      break;
    case TftViewMode::Navigation:
      if (current[1] != previous[1]) regions.add(7, 5, 210, 90);
      if (current[2] != previous[2]) regions.add(0, 3, 320, 116);
      if (current[3] != previous[3]) regions.add(7, 54, 306, 130);
      if (current[4] != previous[4]) regions.add(7, 96, 306, 75);
      if (current[5] != previous[5]) regions.add(7, 116, 306, 120);
      break;
    case TftViewMode::Weather:
      if (current[1] != previous[1]) regions.add(10, 8, 300, 48);
      if (current[2] != previous[2]) regions.add(8, 58, 304, 180);
      if (current[3] != previous[3]) regions.add(84, 132, 152, 38);
      break;
    case TftViewMode::AutoStatus:
      if (current[1] != previous[1]) regions.add(8, 58, 304, 56);
      if (current[2] != previous[2]) regions.add(8, 116, 304, 49);
      if (current[3] != previous[3]) regions.add(8, 173, 304, 51);
      break;
    case TftViewMode::Settings:
      if (current[1] != previous[1]) {
        regions.add(8, settingsPage == 3 ? 61 : 210, 304,
                    settingsPage == 3 ? 155 : 24);
      }
      if (current[2] != previous[2]) regions.add(8, 64, 304, 142);
      if (current[3] != previous[3]) {
        addSettingsRowRegion(regions, settingsPage, previousPressedSettingsRow);
        addSettingsRowRegion(regions, settingsPage, pressedSettingsRow);
      }
      break;
    default:
      for (uint8_t i = 1; i < 8; ++i) {
        if (current[i] != previous[i]) {
          regions.add(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT);
          break;
        }
      }
      break;
  }

  // Every visual component signature needs a matching compose region.  This
  // guard turns future mapping omissions into a safe one-off full redraw
  // instead of leaving stale pixels on an otherwise cached page.
  if (regions.count == 0) {
    for (uint8_t i = 1; i < 8; ++i) {
      if (current[i] != previous[i]) {
        regions.add(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT);
        break;
      }
    }
  }
}

void calculateHomeTransitionBounds(int16_t offsetY, TftViewMode sourceView,
                                   uint8_t settingsPage, int16_t homeScroll,
                                   int16_t& left, int16_t& top,
                                   int16_t& width, int16_t& height) {
  const float progress = constrain(static_cast<float>(AMAP_TFT_HEIGHT - offsetY) /
                                       AMAP_TFT_HEIGHT,
                                   0.0f, 1.0f);
  int16_t targetLeft = 22;
  int16_t targetTop = 66 - homeScroll;
  if (sourceView == TftViewMode::Music) {
    targetLeft = 176;
  } else if (sourceView == TftViewMode::Weather) {
    targetTop = 139 - homeScroll;
  } else if (sourceView == TftViewMode::Settings) {
    targetLeft = settingsPage == 1 ? 176 : 22;
    targetTop = 212 - homeScroll;
  } else if (sourceView == TftViewMode::Auto || sourceView == TftViewMode::AutoStatus) {
    targetLeft = 176;
    targetTop = 139 - homeScroll;
  }
  width = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_WIDTH -
      progress * (AMAP_TFT_WIDTH - 38)));
  height = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_HEIGHT -
      progress * (AMAP_TFT_HEIGHT - 38)));
  left = static_cast<int16_t>(progress * targetLeft);
  top = static_cast<int16_t>(progress * targetTop);
}

void addPhoneSheetVisibleBounds(TftRenderRegions& regions, int16_t offsetY) {
  const int16_t top = max<int16_t>(0, offsetY);
  const int16_t bottom = min<int16_t>(AMAP_TFT_HEIGHT, AMAP_TFT_HEIGHT + offsetY);
  if (bottom > top) regions.add(0, top, AMAP_TFT_WIDTH, bottom - top);
}
}  // namespace

TftRenderer::Canvas::Canvas() : Adafruit_GFX(AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT) {}

TftRenderer::Canvas::~Canvas() {
  free(buffer);
}

bool TftRenderer::Canvas::begin() {
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

uint16_t* TftRenderer::Canvas::pixels() {
  return buffer;
}

void TftRenderer::Canvas::swapBuffer(Canvas& other) {
  uint16_t* temporary = buffer;
  buffer = other.buffer;
  other.buffer = temporary;
}

void TftRenderer::Canvas::setRenderRegions(const TftRenderRegions* regions) {
  renderRegions = regions;
}

void TftRenderer::Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (buffer != nullptr && x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT &&
      (renderRegions == nullptr || renderRegions->contains(x, y))) {
    buffer[y * WIDTH + x] = __builtin_bswap16(color);
  }
}

void TftRenderer::Canvas::drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) {
  if (buffer == nullptr || y < 0 || y >= HEIGHT || width <= 0) return;
  if (x < 0) { width += x; x = 0; }
  if (x + width > WIDTH) width = WIDTH - x;
  const uint16_t panelColor = __builtin_bswap16(color);
  if (renderRegions == nullptr) {
    for (int16_t i = 0; i < width; ++i) buffer[y * WIDTH + x + i] = panelColor;
    return;
  }
  for (uint8_t regionIndex = 0; regionIndex < renderRegions->count; ++regionIndex) {
    const TftRenderRect& region = renderRegions->rects[regionIndex];
    if (y < region.y || y >= region.y + region.height) continue;
    const int16_t left = max<int16_t>(x, region.x);
    const int16_t right = min<int16_t>(x + width, region.x + region.width);
    for (int16_t column = left; column < right; ++column) {
      buffer[y * WIDTH + column] = panelColor;
    }
  }
}

void TftRenderer::Canvas::drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) {
  if (buffer == nullptr || x < 0 || x >= WIDTH || height <= 0) return;
  if (y < 0) { height += y; y = 0; }
  if (y + height > HEIGHT) height = HEIGHT - y;
  const uint16_t panelColor = __builtin_bswap16(color);
  if (renderRegions == nullptr) {
    for (int16_t i = 0; i < height; ++i) buffer[(y + i) * WIDTH + x] = panelColor;
    return;
  }
  for (uint8_t regionIndex = 0; regionIndex < renderRegions->count; ++regionIndex) {
    const TftRenderRect& region = renderRegions->rects[regionIndex];
    if (x < region.x || x >= region.x + region.width) continue;
    const int16_t top = max<int16_t>(y, region.y);
    const int16_t bottom = min<int16_t>(y + height, region.y + region.height);
    for (int16_t row = top; row < bottom; ++row) buffer[row * WIDTH + x] = panelColor;
  }
}

void TftRenderer::Canvas::fillScreen(uint16_t color) {
  if (buffer == nullptr) return;
  const uint16_t panelColor = __builtin_bswap16(color);
  if (renderRegions == nullptr) {
    for (size_t i = 0; i < kPixels; ++i) buffer[i] = panelColor;
    return;
  }
  for (uint8_t regionIndex = 0; regionIndex < renderRegions->count; ++regionIndex) {
    const TftRenderRect& region = renderRegions->rects[regionIndex];
    for (int16_t row = region.y; row < region.y + region.height; ++row) {
      uint16_t* destination = buffer + row * WIDTH + region.x;
      for (int16_t column = 0; column < region.width; ++column) {
        destination[column] = panelColor;
      }
    }
  }
}

TftRenderer::~TftRenderer() {
  if (frameTransferTask != nullptr) vTaskDelete(frameTransferTask);
  if (frameTransferDone != nullptr) vSemaphoreDelete(frameTransferDone);
  free(transferBuffer);
  if (dmaTransferDone != nullptr) vSemaphoreDelete(dmaTransferDone);
}

void TftRenderer::begin() {
  if (AMAP_TFT_SCLK_PIN < 0 || AMAP_TFT_MOSI_PIN < 0 || AMAP_TFT_CS_PIN < 0 ||
      AMAP_TFT_DC_PIN < 0 || AMAP_TFT_RST_PIN < 0 || AMAP_TFT_BL_PIN < 0) {
    Serial.println("TFT disabled: SPI panel pins are not configured");
    return;
  }

  ledcSetup(0, 12000, 8);
  ledcAttachPin(AMAP_TFT_BL_PIN, 0);
  displayPreferences = DisplayPreferences::load();
  applyBrightness();
  const HardwareSettings hardware = HardwareSettings::load();
  if (hardware.tftDriver == AMAP_TFT_DRIVER_ILI9341) {
    SPI.begin(AMAP_TFT_SCLK_PIN, AMAP_TFT_MISO_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
    ili9341.begin(AMAP_TFT_SPI_FREQUENCY);
    ili9341.setRotation(AMAP_TFT_ROTATION);
    ili9341.setSPISpeed(AMAP_TFT_SPI_FREQUENCY);
    ili9341.invertDisplay(hardware.invertColors);
    activePanel = &ili9341;
  } else {
    // ESP32 Arduino's generic SPI.writePixels() refills a 64-byte FIFO in a
    // tight CPU loop. The native LCD driver feeds the same 80 MHz bus with
    // GDMA, cutting full-frame transfer time without changing panel pixels.
    if (!beginSt7789DmaPanel(hardware.invertColors)) {
      Serial.println("TFT disabled: unable to initialise ST7789 DMA panel");
      return;
    }
  }
  transferBuffer = static_cast<uint16_t*>(heap_caps_malloc(
      kTransferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (transferBuffer == nullptr) {
    transferBuffer = static_cast<uint16_t*>(malloc(kTransferBytes));
  }
  if (!canvas.begin() || !previousFrame.begin() || !adjacentFrame.begin() ||
      !homeTransitionFrame.begin() || !transferFrame.begin() || !pageCache.begin() ||
      transferBuffer == nullptr) {
    Serial.println("TFT disabled: unable to allocate frame buffers");
    return;
  }
  frameTransferDone = xSemaphoreCreateBinary();
  if (frameTransferDone == nullptr) {
    Serial.println("TFT disabled: unable to allocate transfer synchronizer");
    return;
  }
  xSemaphoreGive(frameTransferDone);
  if (xTaskCreate(frameTransferTaskEntry, "tft-transfer", 4096, this, 2,
                  &frameTransferTask) != pdPASS) {
    frameTransferTask = nullptr;
    Serial.println("TFT disabled: unable to start transfer task");
    return;
  }
  if (activePanel != nullptr) activePanel->fillScreen(0x0861);
  tftFont.begin(canvas);
  tftFont.setFontMode(1);
  tftFont.setFont(u8g2_font_wqy12_t_gb2312);
  adjacentFont.begin(adjacentFrame);
  adjacentFont.setFontMode(1);
  adjacentFont.setFont(u8g2_font_wqy12_t_gb2312);
  applyBrightness();
  ready = true;
  const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("%s ready: %dx%d, inversion=%s, transfer=%s, 5-frame pipelined, PSRAM=%u/%u free, largest=%u, SPI SCK=%d MOSI=%d CS=%d\n",
                hardware.tftDriverName(), AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT,
                hardware.invertColors ? "on" : "off",
                dmaPanel != nullptr ? "GDMA" : "FIFO",
                static_cast<unsigned>(psramFree), static_cast<unsigned>(psramTotal),
                static_cast<unsigned>(psramLargest),
                AMAP_TFT_SCLK_PIN, AMAP_TFT_MOSI_PIN, AMAP_TFT_CS_PIN);
}

bool TftRenderer::isReady() const {
  return ready;
}

void TftRenderer::updateTouch(uint8_t touchCount, int16_t x, int16_t y) {
  if (!ready) {
    return;
  }
  const unsigned long now = millis();
  if (touchCount > 1) {
    if (touching) {
      endTouch(now);
    }
    return;
  }
  if (touchCount > 0) {
    if (!touching) {
      beginTouch(x, y, now);
    } else {
      moveTouch(x, y, now);
    }
  } else if (touching) {
    endTouch(now);
  }
}

bool TftRenderer::takeMediaControlCommand(MediaControlCommand& command) {
  if (pendingMediaControl == MediaControlCommand::None) {
    return false;
  }
  command = pendingMediaControl;
  pendingMediaControl = MediaControlCommand::None;
  return true;
}

bool TftRenderer::takeWeatherRetryRequest() {
  if (!pendingWeatherRetry) return false;
  pendingWeatherRetry = false;
  return true;
}

const char* TftRenderer::currentViewName() const {
  switch (viewMode) {
    case TftViewMode::Home: return "home";
    case TftViewMode::Navigation: return "navigation";
    case TftViewMode::Music: return "music";
    case TftViewMode::Weather: return "weather";
    case TftViewMode::Settings: return "settings";
    case TftViewMode::AutoStatus: return "auto_status";
    default: return "auto";
  }
}

void TftRenderer::beginTouch(int16_t x, int16_t y, unsigned long now) {
  // A rendered transition may already be at its last pixel while its spring
  // still has a tiny invisible velocity. Settle it before hit-testing so the
  // page under the finger always owns the touch.
  if (homeTransitionVisible) {
    const bool commitHome = homeTransitionTargetY == 0;
    Serial.printf("touch settled home transition: target=%s offset=%d\n",
                  commitHome ? "home" : "app", homeTransitionOffsetY);
    finishHomeTransition(commitHome, now);
  }
  touching = true;
  directionLocked = false;
  horizontalGesture = false;
  horizontalBlocked = false;
  phoneSheetGesture = false;
  phoneSheetAnimationLocked = phoneSheetSpringActive;
  homeGesture = false;
  homeScrollGesture = false;
  weatherRetryPressed = viewMode == TftViewMode::Weather && weatherRetryAvailable &&
                        hitTestWeatherRetry(x, y);
  springActive = false;
  touchStartX = x;
  touchStartY = y;
  touchBaseOffset = dragOffsetX;
  homeScrollStart = homeScroll;
  lastSampleX = x;
  lastSampleY = y;
  releaseVelocityX = 0.0f;
  releaseVelocityY = 0.0f;
  touchStartedAt = now;
  lastSampleAt = now;
  gestureHintUntil = now + 650UL;
  pressedMediaControl = musicControlsVisible ? hitTestMediaControl(x, y)
                                             : MediaControlCommand::None;
  settingsRow = -1;
  autoStatusPressed = viewMode == TftViewMode::AutoStatus && x >= 12 && x <= 308 &&
                      y >= 177 && y <= 220;
  if (viewMode == TftViewMode::Settings) {
    if (settingsPage > 0 && x >= 12 && x <= 66 && y >= 10 && y <= 35) {
      settingsRow = -2;
    } else if (settingsPage == 0 && x >= 8 && x <= 312 && y >= 58 && y <= 211) {
      settingsRow = static_cast<int8_t>((y - 58) / 38);
    } else if (settingsPage == 1 && x >= 8 && x <= 312 && y >= 70 && y <= 189) {
      settingsRow = static_cast<int8_t>((y - 70) / 42);
    } else if (settingsPage == 2 && x >= 8 && x <= 312 && y >= 70 && y <= 147) {
      settingsRow = static_cast<int8_t>((y - 70) / 42);
    } else if (settingsPage == 4 && x >= 8 && x <= 312 && y >= 70 && y <= 105) {
      settingsRow = 0;
    }
  }
}

void TftRenderer::moveTouch(int16_t x, int16_t y, unsigned long now) {
  const int16_t deltaX = x - touchStartX;
  const int16_t deltaY = y - touchStartY;
  if (!directionLocked && (abs(deltaX) >= 10 || abs(deltaY) >= 10)) {
    directionLocked = true;
    // Horizontal swipes intentionally have no navigation role. Reserve the
    // vertical edges for the two system gestures, then let the desktop list
    // consume ordinary vertical drags.
    horizontalGesture = false;
    phoneSheetGesture = !phoneSheetAnimationLocked &&
                        (phoneSheetVisible || (touchStartY <= 60 && deltaY > 0 &&
                                               abs(deltaY) >= abs(deltaX)));
    homeGesture = !phoneSheetGesture && !phoneSheetVisible && viewMode != TftViewMode::Home &&
                  touchStartY >= AMAP_TFT_HEIGHT - 42 && deltaY < 0;
    homeScrollGesture = !phoneSheetGesture && !homeGesture &&
                        viewMode == TftViewMode::Home &&
                        abs(deltaY) >= abs(deltaX);
    // The notification sheet settles once released; a later touch may not
    // grab it mid-flight. The desktop return remains directly manipulable.
    if (homeGesture) homeTransitionSpringActive = false;
    if (homeGesture && !homeTransitionSnapshotReady) {
      memcpy(homeTransitionFrame.pixels(), previousFrame.pixels(), kPixelBytes);
      homeTransitionSnapshotReady = true;
      homeTransitionDestinationReady = false;
    }
    if (phoneSheetGesture) {
      phoneSheetSnapshotReady = false;
      if (!phoneSheetVisible) {
        memcpy(homeTransitionFrame.pixels(), previousFrame.pixels(), kPixelBytes);
        phoneSheetBaseSnapshotReady = true;
      } else {
        phoneSheetBaseSnapshotReady = false;
      }
    }
  }
  if (directionLocked) {
    pressedMediaControl = MediaControlCommand::None;
    weatherRetryPressed = false;
    settingsRow = -1;
    autoStatusPressed = false;
  }
  const unsigned long elapsed = now - lastSampleAt;
  if (elapsed > 0 && x != lastSampleX) {
    const float sampleVelocity = static_cast<float>(x - lastSampleX) * 1000.0f / elapsed;
    releaseVelocityX = releaseVelocityX * 0.55f + sampleVelocity * 0.45f;
  }
  if (elapsed > 0 && y != lastSampleY) {
    const float sampleVelocity = static_cast<float>(y - lastSampleY) * 1000.0f / elapsed;
    releaseVelocityY = releaseVelocityY * 0.55f + sampleVelocity * 0.45f;
  }
  if (directionLocked && phoneSheetGesture) {
    const int16_t rawOffset = phoneSheetVisible
        ? deltaY : static_cast<int16_t>(-AMAP_TFT_HEIGHT + max<int16_t>(0, deltaY));
    // Resist a pull beyond the open/closed endpoints instead of hard-stopping.
    if (rawOffset > 0) {
      phoneSheetOffsetY = rawOffset / 3;
    } else if (rawOffset < -AMAP_TFT_HEIGHT) {
      phoneSheetOffsetY = -AMAP_TFT_HEIGHT + (rawOffset + AMAP_TFT_HEIGHT) / 3;
    } else {
      phoneSheetOffsetY = rawOffset;
    }
    phoneSheetVisible = true;
    gestureHintUntil = now + 350UL;
  } else if (directionLocked && homeGesture) {
    const int16_t rawOffset = static_cast<int16_t>(AMAP_TFT_HEIGHT + min<int16_t>(0, deltaY));
    homeTransitionOffsetY = max<int16_t>(0, rawOffset);
    homeTransitionVisible = true;
    gestureHintUntil = now + 350UL;
  } else if (directionLocked && homeScrollGesture) {
    homeScroll = constrain(homeScrollStart - deltaY, 0, 33);
    frameDrawn = false;
  }
  lastSampleX = x;
  lastSampleY = y;
  lastSampleAt = now;
}

void TftRenderer::endTouch(unsigned long now) {
  const int16_t deltaY = lastSampleY - touchStartY;
  const unsigned long duration = now - touchStartedAt;
  touching = false;
  if (!directionLocked && pressedMediaControl != MediaControlCommand::None &&
      hitTestMediaControl(lastSampleX, lastSampleY) == pressedMediaControl &&
      duration < 700UL) {
    pendingMediaControl = pressedMediaControl;
    Serial.printf("touch media control: %s\n", mediaControlAction(pendingMediaControl));
    pressedMediaControl = MediaControlCommand::None;
    frameDrawn = false;
    gestureHintUntil = 0;
    return;
  }
  if (!directionLocked && weatherRetryPressed && duration < 700UL &&
      hitTestWeatherRetry(lastSampleX, lastSampleY)) {
    pendingWeatherRetry = true;
    weatherRetryPressed = false;
    frameDrawn = false;
    gestureHintUntil = 0;
    return;
  }
  weatherRetryPressed = false;
  if (!directionLocked && settingsRow >= 0 && duration < 700UL &&
      viewMode == TftViewMode::Settings && !homeTransitionVisible) {
    cycleSettingsRow();
    settingsRow = -1;
    return;
  }
  if (!directionLocked && settingsRow == -2 && duration < 700UL &&
      viewMode == TftViewMode::Settings && !homeTransitionVisible) {
    settingsPage = 0;
    settingsRow = -1;
    frameDrawn = false;
    return;
  }
  if (!directionLocked && autoStatusPressed && duration < 700UL &&
      viewMode == TftViewMode::AutoStatus && lastSampleY >= 177 && lastSampleY <= 220) {
    displayPreferences.autoView = !displayPreferences.autoView;
    displayPreferences.save();
    // "Auto" is its own status app. Changing the background preference must
    // not replace this screen with the navigation renderer.
    frameDrawn = false;
    return;
  }
  // The detail view promises "tap to return".  Previously only a tiny area at
  // the very bottom could dismiss it, while every vertical drag was consumed
  // as text scrolling.  Let a normal tap anywhere in the detail return to the
  // phone overview, so the user always has an immediate way out.
  if (!directionLocked && duration < 700UL && phoneSheetVisible && phoneDetail) {
    phoneDetail = false;
    phoneDetailScroll = 0;
    frameDrawn = false;
    gestureHintUntil = 0;
    return;
  }
  if (!directionLocked && duration < 700UL && phoneSheetVisible && lastSampleY >= 195) {
    phoneDetail = true;
    phoneDetailScroll = 0;
    frameDrawn = false;
    return;
  }
  pressedMediaControl = MediaControlCommand::None;
  if (directionLocked && phoneSheetGesture) {
    if (phoneDetail && touchStartY > 42 && abs(deltaY) >= 28 && phoneSheetOffsetY == 0) {
      phoneDetailScroll = constrain(static_cast<int>(phoneDetailScroll) + (deltaY < 0 ? 3 : -3), 0, 24);
      frameDrawn = false;
      return;
    }
    constexpr int16_t kEdgeCommitDistance = 60;
    const bool open = phoneSheetOffsetY >= -AMAP_TFT_HEIGHT + kEdgeCommitDistance ||
                      releaseVelocityY > 360.0f;
    const bool close = phoneSheetOffsetY <= -kEdgeCommitDistance || releaseVelocityY < -360.0f;
    phoneSheetTargetY = phoneSheetOffsetY > -AMAP_TFT_HEIGHT / 2
                            ? (close ? -AMAP_TFT_HEIGHT : 0)
                            : (open ? 0 : -AMAP_TFT_HEIGHT);
    phoneSheetVelocity = releaseVelocityY;
    phoneSheetSpringActive = true;
    lastSpringAt = now;
  } else if (directionLocked && homeGesture) {
    constexpr int16_t kHomeCommitDistance = 60;
    const bool returnHome = homeTransitionOffsetY <= AMAP_TFT_HEIGHT - kHomeCommitDistance ||
                            releaseVelocityY < -360.0f;
    homeTransitionTargetY = returnHome ? 0 : AMAP_TFT_HEIGHT;
    homeTransitionVelocity = releaseVelocityY;
    homeTransitionSpringActive = true;
    lastSpringAt = now;
  } else if (directionLocked && homeScrollGesture) {
    frameDrawn = false;
  } else if (directionLocked && !phoneSheetAnimationLocked && !horizontalGesture &&
             phoneSheetVisible && phoneDetail) {
    phoneDetailScroll = constrain(static_cast<int>(phoneDetailScroll) + (deltaY < 0 ? 3 : -3), 0, 24);
    frameDrawn = false;
  } else if (!directionLocked && duration < 700UL && viewMode == TftViewMode::Home) {
    const int16_t contentY = lastSampleY + homeScroll;
    if (contentY >= 201 && contentY <= 263) {
      switchView(TftViewMode::Settings, now);
      settingsPage = lastSampleX < 160 ? 3 : 1;
    } else {
      const TftViewMode app = hitTestHomeApp(lastSampleX, contentY);
      if (app != TftViewMode::Home) switchView(app, now);
    }
  } else if (directionLocked && abs(lastSampleX - touchStartX) > abs(deltaY)) {
    // Horizontal drags deliberately do not navigate or reveal another view.
    gestureHintUntil = 0;
  } else {
    gestureHintUntil = now + 1200UL;
  }
}

void TftRenderer::applyBrightness() {
  const uint32_t duty = static_cast<uint32_t>(displayPreferences.effectiveBrightness()) * 255U / 100U;
  ledcWrite(0, duty);
}

void TftRenderer::recordRenderedFrame(unsigned long now) {
  if (!displayPreferences.showFrameRate) return;
  if (fpsWindowStartedAt == 0) fpsWindowStartedAt = now;
  ++fpsFrameCount;
  const unsigned long elapsed = now - fpsWindowStartedAt;
  if (elapsed < 1000UL) return;
  framesPerSecond = static_cast<uint16_t>(fpsFrameCount * 1000UL / max<unsigned long>(1, elapsed));
  fpsWindowStartedAt = now;
  fpsFrameCount = 0;
}

void TftRenderer::cycleSettingsRow() {
  if (settingsPage == 0) {
    settingsPage = constrain(static_cast<int>(settingsRow) + 1, 1, 4);
    frameDrawn = false;
    return;
  }
  const int row = constrain(static_cast<int>(settingsRow), 0, settingsPage == 1 ? 2 : 1);
  if (settingsPage == 1) {
    if (row == 0) displayPreferences.brightness = displayPreferences.brightness >= 100
        ? 20 : displayPreferences.brightness + 20;
    else if (row == 1) displayPreferences.nightDim = !displayPreferences.nightDim;
    else {
      const uint8_t next =
          (static_cast<uint8_t>(displayPreferences.musicPageStyle) + 1U) % 3U;
      displayPreferences.musicPageStyle = static_cast<MusicPageStyle>(next);
    }
  } else if (settingsPage == 2) {
    if (row == 0) {
      displayPreferences.autoView = !displayPreferences.autoView;
      if (!displayPreferences.autoView && viewMode == TftViewMode::Auto) viewMode = TftViewMode::Home;
    } else {
      displayPreferences.messageBanners = !displayPreferences.messageBanners;
    }
  } else if (settingsPage == 4) {
    displayPreferences.showFrameRate = !displayPreferences.showFrameRate;
    fpsWindowStartedAt = millis();
    fpsFrameCount = 0;
    framesPerSecond = 0;
  }
  displayPreferences.save();
  applyBrightness();
  frameDrawn = false;
}

void TftRenderer::advanceSpring(unsigned long now) {
  if (!springActive || touching) {
    return;
  }
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) {
    return;
  }
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  const float position = dragOffsetX;
  constexpr float omega = 19.0f;
  const float acceleration = omega * omega * (springTarget - position) -
                             2.0f * omega * springVelocity;
  springVelocity += acceleration * dt;
  float next = position + springVelocity * dt;
  if ((springTarget > 0 && next > springTarget) ||
      (springTarget < 0 && next < springTarget)) {
    next = springTarget;
    springVelocity = 0.0f;
  }
  dragOffsetX = static_cast<int16_t>(roundf(next));
  if (abs(springTarget - dragOffsetX) <= 1 && abs(springVelocity) < 12.0f) {
    dragOffsetX = springTarget;
    if (springTarget == 0) {
      springActive = false;
      springVelocity = 0.0f;
      gestureHintUntil = now + 700UL;
    } else {
      finishHorizontalTransition();
    }
  }
}

void TftRenderer::advancePhoneSheetSpring(unsigned long now) {
  if (!phoneSheetSpringActive) {
    return;
  }
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) return;
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  constexpr float omega = 20.0f;  // response ~0.35 s, critically damped
  const float position = phoneSheetOffsetY;
  const float acceleration = omega * omega * (phoneSheetTargetY - position) -
                             2.0f * omega * phoneSheetVelocity;
  phoneSheetVelocity += acceleration * dt;
  float next = position + phoneSheetVelocity * dt;
  if ((phoneSheetTargetY == 0 && next > 0) ||
      (phoneSheetTargetY == -AMAP_TFT_HEIGHT && next < -AMAP_TFT_HEIGHT)) {
    next = phoneSheetTargetY;
    phoneSheetVelocity = 0.0f;
  }
  phoneSheetOffsetY = static_cast<int16_t>(roundf(next));
  if (abs(phoneSheetTargetY - phoneSheetOffsetY) <= 1 && abs(phoneSheetVelocity) < 12.0f) {
    phoneSheetOffsetY = phoneSheetTargetY;
    phoneSheetSpringActive = false;
    phoneSheetVelocity = 0.0f;
    phoneSheetVisible = phoneSheetOffsetY > -AMAP_TFT_HEIGHT;
    phoneSheetBaseSnapshotReady = false;
    phoneSheetSnapshotReady = false;
    gestureHintUntil = now + 700UL;
  }
}

void TftRenderer::advanceHomeTransitionSpring(unsigned long now) {
  if (!homeTransitionSpringActive || touching) return;
  const unsigned long elapsedMs = min<unsigned long>(now - lastSpringAt, 34UL);
  if (elapsedMs == 0) return;
  lastSpringAt = now;
  const float dt = elapsedMs / 1000.0f;
  // A critically damped analytic spring keeps the app exit brisk without the
  // long tail or numerical wobble that a larger Euler step would introduce.
  constexpr float omega = 28.0f;
  const float position = homeTransitionOffsetY;
  const float displacement = position - homeTransitionTargetY;
  const float c = homeTransitionVelocity + omega * displacement;
  const float decay = expf(-omega * dt);
  float next = homeTransitionTargetY + (displacement + c * dt) * decay;
  homeTransitionVelocity = (homeTransitionVelocity - omega * c * dt) * decay;
  if ((homeTransitionTargetY == 0 && next <= 0.0f) ||
      (homeTransitionTargetY == AMAP_TFT_HEIGHT && next >= AMAP_TFT_HEIGHT)) {
    next = homeTransitionTargetY;
    homeTransitionVelocity = 0.0f;
  }
  homeTransitionOffsetY = static_cast<int16_t>(roundf(next));
  // Integer-pixel rendering cannot show the remaining sub-pixel spring tail.
  // Commit as soon as the visible endpoint is reached so input state cannot
  // lag behind the screen by another frame.
  if (abs(homeTransitionTargetY - homeTransitionOffsetY) <= 1) {
    finishHomeTransition(homeTransitionTargetY == 0, now);
  }
}

void TftRenderer::finishHomeTransition(bool commitHome, unsigned long now) {
  homeTransitionOffsetY = AMAP_TFT_HEIGHT;
  homeTransitionSpringActive = false;
  homeTransitionVisible = false;
  homeTransitionSnapshotReady = false;
  homeTransitionDestinationReady = false;
  homeTransitionVelocity = 0.0f;
  settingsRow = -1;
  autoStatusPressed = false;
  weatherRetryPressed = false;
  pressedMediaControl = MediaControlCommand::None;
  if (commitHome) viewMode = TftViewMode::Home;
  gestureHintUntil = now + 700UL;
  frameDrawn = false;
}

void TftRenderer::switchView(TftViewMode mode, unsigned long now) {
  viewMode = mode;
  if (mode == TftViewMode::Settings) settingsPage = 0;
  springActive = false;
  homeTransitionSpringActive = false;
  homeTransitionVisible = false;
  homeTransitionSnapshotReady = false;
  homeTransitionDestinationReady = false;
  homeTransitionOffsetY = AMAP_TFT_HEIGHT;
  springVelocity = 0.0f;
  springTarget = 0;
  gestureHintUntil = now + 1200UL;
  Serial.printf("touch view: %s\n", currentViewName());
}

void TftRenderer::finishHorizontalTransition() {
  const int direction = springTarget < 0 ? 1 : -1;
  viewMode = adjacentView(direction);
  dragOffsetX = 0;
  springTarget = 0;
  springVelocity = 0.0f;
  springActive = false;
  gestureHintUntil = millis() + 1000UL;
  Serial.printf("touch view: %s\n", currentViewName());
}

TftViewMode TftRenderer::adjacentView(int direction) const {
  const TftViewMode pages[] = {TftViewMode::Home, TftViewMode::Navigation,
                               TftViewMode::Music, TftViewMode::Settings};
  int index = 0;
  for (uint8_t candidate = 0; candidate < 4; ++candidate) {
    if (pages[candidate] == viewMode) {
      index = candidate;
      break;
    }
  }
  return pages[constrain(index + direction, 0, 3)];
}

TftViewMode TftRenderer::resolveAutoView(const NavState& state, bool connected,
                                         unsigned long silenceMs) const {
  if (!connected || silenceMs > AMAP_STALE_MS) return TftViewMode::Home;
  if (state.active) return TftViewMode::Navigation;
  if (state.music.active) return TftViewMode::Music;
  return TftViewMode::Home;
}

TftViewMode TftRenderer::hitTestHomeApp(int16_t x, int16_t y) const {
  if (y >= 55 && y <= 117) return x < 160 ? TftViewMode::Navigation : TftViewMode::Music;
  if (y >= 128 && y <= 190) return x < 160 ? TftViewMode::Weather : TftViewMode::AutoStatus;
  return TftViewMode::Home;
}

MediaControlCommand TftRenderer::hitTestMediaControl(int16_t x, int16_t y) const {
  if (!musicControlsVisible || y < 195 || y > 232) {
    return MediaControlCommand::None;
  }
  if (displayPreferences.musicPageStyle == MusicPageStyle::RefinedNowPlaying) {
    if (x >= 98 && x <= 139) return MediaControlCommand::Previous;
    if (x >= 140 && x <= 181) return MediaControlCommand::PlayPause;
    if (x >= 182 && x <= 223) return MediaControlCommand::Next;
    return MediaControlCommand::None;
  }
  if (x >= 15 && x <= 56) return MediaControlCommand::Previous;
  if (x >= 57 && x <= 98) return MediaControlCommand::PlayPause;
  if (x >= 99 && x <= 141) return MediaControlCommand::Next;
  return MediaControlCommand::None;
}

bool TftRenderer::hitTestWeatherRetry(int16_t x, int16_t y) const {
  return x >= 88 && x <= 232 && y >= 136 && y <= 166;
}

void TftRenderer::compositeHorizontalSlide(int16_t offset) {
  if (offset == 0) {
    return;
  }
  const int16_t shift = min<int16_t>(abs(offset), AMAP_TFT_WIDTH - 1);
  uint16_t* current = canvas.pixels();
  const uint16_t* adjacent = adjacentFrame.pixels();
  for (int16_t row = 0; row < AMAP_TFT_HEIGHT; ++row) {
    uint16_t* currentRow = current + row * AMAP_TFT_WIDTH;
    const uint16_t* adjacentRow = adjacent + row * AMAP_TFT_WIDTH;
    if (offset < 0) {
      memmove(currentRow, currentRow + shift,
              (AMAP_TFT_WIDTH - shift) * sizeof(uint16_t));
      memcpy(currentRow + AMAP_TFT_WIDTH - shift, adjacentRow,
             shift * sizeof(uint16_t));
    } else {
      memmove(currentRow + shift, currentRow,
              (AMAP_TFT_WIDTH - shift) * sizeof(uint16_t));
      memcpy(currentRow, adjacentRow + AMAP_TFT_WIDTH - shift,
             shift * sizeof(uint16_t));
    }
  }
}

void TftRenderer::compositePhoneSheet(int16_t offsetY) {
  if (offsetY <= -AMAP_TFT_HEIGHT) return;
  uint16_t* base = canvas.pixels();
  const uint16_t* sheet = adjacentFrame.pixels();
  const int16_t firstSheetRow = max<int16_t>(0, -offsetY);
  const int16_t lastSheetRow = min<int16_t>(AMAP_TFT_HEIGHT, AMAP_TFT_HEIGHT - offsetY);
  for (int16_t sheetRow = firstSheetRow; sheetRow < lastSheetRow; ++sheetRow) {
    const int16_t destinationRow = sheetRow + offsetY;
    memcpy(base + destinationRow * AMAP_TFT_WIDTH, sheet + sheetRow * AMAP_TFT_WIDTH,
           AMAP_TFT_WIDTH * sizeof(uint16_t));
  }
}

void TftRenderer::compositeHomeTransition(int16_t offsetY) {
  if (offsetY >= AMAP_TFT_HEIGHT) return;
  uint16_t* base = canvas.pixels();
  const uint16_t* home = adjacentFrame.pixels();
  const uint16_t* app = homeTransitionFrame.pixels();
  memcpy(base, home, kPixelBytes);
  const float progress = constrain(static_cast<float>(AMAP_TFT_HEIGHT - offsetY) /
                                       AMAP_TFT_HEIGHT,
                                   0.0f, 1.0f);
  int16_t targetLeft = 22;
  int16_t targetTop = 66 - homeScroll;
  const char* iconApp = "map";
  uint16_t iconSurface = 0x03EC;
  if (viewMode == TftViewMode::Music) {
    targetLeft = 176;
    iconApp = "music";
    iconSurface = 0x715C;
  } else if (viewMode == TftViewMode::Weather) {
    targetTop = 139 - homeScroll;
    iconApp = "weather";
    iconSurface = 0xFC40;
  }
  else if (viewMode == TftViewMode::Settings) {
    targetLeft = settingsPage == 1 ? 176 : 22;
    targetTop = 212 - homeScroll;
    iconApp = settingsPage == 1 ? "display" : "settings";
    iconSurface = settingsPage == 1 ? 0x715C : 0x014A;
  } else if (viewMode == TftViewMode::Auto || viewMode == TftViewMode::AutoStatus) {
    targetLeft = 176;
    targetTop = 139 - homeScroll;
    iconApp = "auto";
    iconSurface = 0x04DF;
  }
  const int16_t width = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_WIDTH -
      progress * (AMAP_TFT_WIDTH - 38)));
  const int16_t height = max<int16_t>(38, static_cast<int16_t>(AMAP_TFT_HEIGHT -
      progress * (AMAP_TFT_HEIGHT - 38)));
  const int16_t left = static_cast<int16_t>(progress * targetLeft);
  const int16_t top = static_cast<int16_t>(progress * targetTop);
  // Build the exact nearest-neighbour map once per axis. The old inner loop
  // performed two integer divisions for every destination pixel (up to
  // 76,800 divisions per frame), which was visible as uneven return motion.
  int16_t sourceXs[AMAP_TFT_WIDTH];
  int16_t sourceYs[AMAP_TFT_HEIGHT];
  for (int16_t x = 0; x < width; ++x) {
    sourceXs[x] = static_cast<int32_t>(x) * AMAP_TFT_WIDTH / width;
  }
  for (int16_t y = 0; y < height; ++y) {
    sourceYs[y] = static_cast<int32_t>(y) * AMAP_TFT_HEIGHT / height;
  }
  for (int16_t y = 0; y < height && top + y < AMAP_TFT_HEIGHT; ++y) {
    uint16_t* destination = base + (top + y) * AMAP_TFT_WIDTH + left;
    const uint16_t* source = app + sourceYs[y] * AMAP_TFT_WIDTH;
    for (int16_t x = 0; x < width && left + x < AMAP_TFT_WIDTH; ++x) {
      destination[x] = source[sourceXs[x]];
    }
  }
  // The source app first shrinks toward its launcher tile, then the tile's
  // own icon grows over it until the final frame is exactly that app icon.
  const float iconProgress = constrain((progress - 0.55f) / 0.45f, 0.0f, 1.0f);
  if (iconProgress > 0.0f) {
    const int16_t iconSize = static_cast<int16_t>(8 + iconProgress * 30.0f);
    const int16_t iconLeft = targetLeft + (38 - iconSize) / 2;
    const int16_t iconTop = targetTop + (38 - iconSize) / 2;
    TftFrameRenderer::drawAppIcon(canvas, iconLeft, iconTop, iconSize, iconApp, iconSurface);
  }
}

void TftRenderer::frameTransferTaskEntry(void* context) {
  TftRenderer* renderer = static_cast<TftRenderer*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint32_t startedAt = micros();
    renderer->pushRectangle(0, 0, AMAP_TFT_WIDTH, AMAP_TFT_HEIGHT,
                            renderer->previousFrame.pixels(), AMAP_TFT_WIDTH);
    renderer->lastTransferDurationUs = micros() - startedAt;
    xSemaphoreGive(renderer->frameTransferDone);
  }
}

void TftRenderer::waitForFrameTransfer() {
  if (frameTransferDone == nullptr) return;
  xSemaphoreTake(frameTransferDone, portMAX_DELAY);
  xSemaphoreGive(frameTransferDone);
}

void TftRenderer::queueFullFrameTransfer() {
  xSemaphoreTake(frameTransferDone, portMAX_DELAY);
  // Rotate ownership instead of copying the 153 KB frame twice. The transfer
  // task owns previousFrame until it signals completion; canvas immediately
  // receives a free buffer for composing the next frame.
  canvas.swapBuffer(transferFrame);
  transferFrame.swapBuffer(previousFrame);
  xTaskNotifyGive(frameTransferTask);
}

void TftRenderer::render(const NavState& state, bool wifiConnected, bool bleConnected,
                         const String& ip, uint16_t port, unsigned long silenceMs,
                         const WeatherState& weather) {
  if (!ready) {
    return;
  }
  const bool connected = wifiConnected || bleConnected;
  const unsigned long now = millis();
  advanceSpring(now);
  advancePhoneSheetSpring(now);
  advanceHomeTransitionSpring(now);
  const TftViewMode renderedView = viewMode == TftViewMode::Auto && displayPreferences.autoView
      ? resolveAutoView(state, connected, silenceMs)
      : (viewMode == TftViewMode::Auto ? TftViewMode::Home : viewMode);
  const bool automaticMode = displayPreferences.autoView;
  weatherRetryAvailable = renderedView == TftViewMode::Weather && weather.configured &&
                          !weather.valid && !weather.loading && !weather.error.isEmpty();
  if (!weatherRetryAvailable) weatherRetryPressed = false;
  const int8_t renderedPressedRow = viewMode == TftViewMode::AutoStatus && autoStatusPressed
      ? 0 : settingsRow;
  musicControlsVisible = displayPreferences.musicPageStyle != MusicPageStyle::PipWindow &&
      state.music.active && connected &&
      ((renderedView == TftViewMode::Music && silenceMs <= AMAP_STANDBY_MS) ||
       (renderedView == TftViewMode::Auto && silenceMs <= AMAP_STALE_MS && !state.active));
  const bool showGestureHint = pressedMediaControl == MediaControlCommand::None &&
                               (touching || springActive || phoneSheetSpringActive ||
                                homeTransitionSpringActive ||
                                static_cast<long>(gestureHintUntil - now) > 0);
  uint32_t signature =
      frameSignature(state, wifiConnected, bleConnected, ip, port, silenceMs, now,
                       renderedView, dragOffsetX, showGestureHint, pressedMediaControl, renderedPressedRow,
                       phoneDetail, phoneDetailScroll, phoneSheetVisible, phoneSheetOffsetY,
                       automaticMode, homeTransitionVisible, homeTransitionOffsetY, settingsPage,
                       homeScroll, weatherRetryPressed, weather,
                       displayPreferences.musicPageStyle, displayPreferences);
  if (displayPreferences.showFrameRate) {
    // The diagnostic badge itself changes at most once per second. Dynamic
    // pages still render at their natural cadence and record every frame, but
    // a static page no longer burns CPU merely to redraw an unchanged FPS label.
    const unsigned long fpsSecond = now / 1000UL;
    hashValue(signature, fpsSecond);
  }
  if (frameDrawn && signature == lastFrameSignature) {
    return;
  }

  const uint32_t frameStartedAt = micros();
  TftRenderRegions composeRegions;
  TftRenderRegions overlayRegions;
  bool usedPageCache = false;
  bool useExplicitDirtyScan = false;
  if (homeTransitionVisible) {
    // The source app and destination desktop are static for the short return
    // animation. Render the desktop once instead of rebuilding both complete
    // pages before every composite frame.
    if (!homeTransitionDestinationReady) {
      TftFrameRenderer::render(adjacentFrame, adjacentFont, state, wifiConnected, bleConnected,
                                ip, port, silenceMs, weather, TftViewMode::Home,
                               MediaControlCommand::None, -1, false, 0, false, 0, homeScroll,
                               false, MusicPageStyle::Standard, &displayPreferences);
      homeTransitionDestinationReady = true;
    }
    int16_t currentLeft = 0;
    int16_t currentTop = 0;
    int16_t currentWidth = 0;
    int16_t currentHeight = 0;
    calculateHomeTransitionBounds(homeTransitionOffsetY, viewMode, settingsPage,
                                  homeScroll, currentLeft, currentTop,
                                  currentWidth, currentHeight);
    if (lastHomeTransitionBoundsValid) {
      composeRegions.add(lastHomeTransitionLeft, lastHomeTransitionTop,
                         lastHomeTransitionWidth, lastHomeTransitionHeight);
      composeRegions.add(currentLeft, currentTop, currentWidth, currentHeight);
      useExplicitDirtyScan = true;
    }
    lastHomeTransitionLeft = currentLeft;
    lastHomeTransitionTop = currentTop;
    lastHomeTransitionWidth = currentWidth;
    lastHomeTransitionHeight = currentHeight;
    lastHomeTransitionBoundsValid = true;
    compositeHomeTransition(homeTransitionOffsetY);
  } else if (phoneSheetVisible) {
    const uint32_t currentSheetSignature = phoneSheetSignature(
        state.phone, wifiConnected, bleConnected, phoneDetail, phoneDetailScroll);
    if (phoneSheetOffsetY == 0 && !phoneSheetSpringActive && !touching) {
      // Once fully open the sheet is the whole frame, so drawing an obscured
      // app underneath would only waste PSRAM bandwidth. Keep its rendered
      // pixels as a snapshot until phone content or detail scroll changes.
      if (!phoneSheetSnapshotReady ||
          currentSheetSignature != phoneSheetContentSignature) {
        TftFrameRenderer::renderPhoneSheet(adjacentFrame, adjacentFont, state.phone,
                                           wifiConnected, bleConnected, phoneDetail,
                                           phoneDetailScroll);
        phoneSheetSnapshotReady = true;
        phoneSheetContentSignature = currentSheetSignature;
      }
      memcpy(canvas.pixels(), adjacentFrame.pixels(), kPixelBytes);
    } else {
      if (!phoneSheetBaseSnapshotReady) {
        TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip, port,
                                 silenceMs, weather, renderedView, pressedMediaControl,
                                 renderedPressedRow, phoneDetail, phoneDetailScroll,
                                 automaticMode, settingsPage, homeScroll, weatherRetryPressed,
                                 displayPreferences.musicPageStyle, &displayPreferences);
        memcpy(homeTransitionFrame.pixels(), canvas.pixels(), kPixelBytes);
        phoneSheetBaseSnapshotReady = true;
      }
      if (!phoneSheetSnapshotReady ||
          currentSheetSignature != phoneSheetContentSignature) {
        TftFrameRenderer::renderPhoneSheet(adjacentFrame, adjacentFont, state.phone,
                                           wifiConnected, bleConnected, phoneDetail,
                                           phoneDetailScroll);
        phoneSheetSnapshotReady = true;
        phoneSheetContentSignature = currentSheetSignature;
      }
      // The sheet and base snapshot are both opaque. Copy each visible row
      // exactly once instead of copying the 153 KB base and then overwriting
      // most of it with the moving sheet.
      if (phoneSheetOffsetY <= 0) {
        const int16_t sheetBottom = max<int16_t>(0, min<int16_t>(AMAP_TFT_HEIGHT,
            AMAP_TFT_HEIGHT + phoneSheetOffsetY));
        if (sheetBottom < AMAP_TFT_HEIGHT) {
          const size_t offset = sheetBottom * AMAP_TFT_WIDTH;
          memcpy(canvas.pixels() + offset, homeTransitionFrame.pixels() + offset,
                 (AMAP_TFT_HEIGHT - sheetBottom) * AMAP_TFT_WIDTH * sizeof(uint16_t));
        }
      } else {
        const int16_t exposedTop = min<int16_t>(phoneSheetOffsetY, AMAP_TFT_HEIGHT);
        memcpy(canvas.pixels(), homeTransitionFrame.pixels(),
               exposedTop * AMAP_TFT_WIDTH * sizeof(uint16_t));
      }
      compositePhoneSheet(phoneSheetOffsetY);
    }
    if (lastPhoneSheetOffsetValid && lastPhoneSheetOffsetY != phoneSheetOffsetY) {
      addPhoneSheetVisibleBounds(composeRegions, lastPhoneSheetOffsetY);
      addPhoneSheetVisibleBounds(composeRegions, phoneSheetOffsetY);
      useExplicitDirtyScan = true;
    }
    lastPhoneSheetOffsetY = phoneSheetOffsetY;
    lastPhoneSheetOffsetValid = true;
  } else {
    uint32_t currentComponents[PAGE_COMPONENT_COUNT];
    buildPageComponents(state, wifiConnected, bleConnected, ip, port, silenceMs, now,
                        renderedView, automaticMode, homeScroll, pressedMediaControl,
                        renderedPressedRow, settingsPage, weatherRetryPressed, weather,
                        displayPreferences, currentComponents);
    usedPageCache = pageCacheValid && pageCacheView == renderedView;
    if (usedPageCache) {
      buildChangedRegions(renderedView, currentComponents, pageComponentSignatures,
                          homeScroll, displayPreferences.musicPageStyle, settingsPage,
                          renderedPressedRow, pageCachePressedSettingsRow,
                          composeRegions);
      if (composeRegions.count > 0) {
        for (uint8_t i = 0; i < composeRegions.count; ++i) {
          const TftRenderRect& region = composeRegions.rects[i];
          for (int16_t row = region.y; row < region.y + region.height; ++row) {
            const size_t offset = row * AMAP_TFT_WIDTH + region.x;
            memcpy(canvas.pixels() + offset, pageCache.pixels() + offset,
                   region.width * sizeof(uint16_t));
          }
        }
        canvas.setRenderRegions(&composeRegions);
        TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip,
                                 port, silenceMs, weather, renderedView, pressedMediaControl,
                                 renderedPressedRow, phoneDetail, phoneDetailScroll,
                                 automaticMode, settingsPage, homeScroll,
                                 weatherRetryPressed, displayPreferences.musicPageStyle,
                                 &displayPreferences, &composeRegions);
        canvas.setRenderRegions(nullptr);
        for (uint8_t i = 0; i < composeRegions.count; ++i) {
          const TftRenderRect& region = composeRegions.rects[i];
          for (int16_t row = region.y; row < region.y + region.height; ++row) {
            const size_t offset = row * AMAP_TFT_WIDTH + region.x;
            memcpy(pageCache.pixels() + offset, canvas.pixels() + offset,
                   region.width * sizeof(uint16_t));
          }
        }
      }
    } else {
      TftFrameRenderer::render(canvas, tftFont, state, wifiConnected, bleConnected, ip,
                               port, silenceMs, weather, renderedView, pressedMediaControl,
                               renderedPressedRow, phoneDetail, phoneDetailScroll,
                               automaticMode, settingsPage, homeScroll,
                               weatherRetryPressed, displayPreferences.musicPageStyle,
                               &displayPreferences);
      memcpy(pageCache.pixels(), canvas.pixels(), kPixelBytes);
      pageCacheValid = true;
      pageCacheView = renderedView;
    }
    memcpy(pageComponentSignatures, currentComponents, sizeof(currentComponents));
    pageCachePressedSettingsRow = renderedPressedRow;
    if (usedPageCache) {
      useExplicitDirtyScan = true;
      if (lastHomeTransitionBoundsValid) {
        composeRegions.add(lastHomeTransitionLeft, lastHomeTransitionTop,
                           lastHomeTransitionWidth, lastHomeTransitionHeight);
      }
      if (lastPhoneSheetOffsetValid) {
        addPhoneSheetVisibleBounds(composeRegions, lastPhoneSheetOffsetY);
      }
    }
    lastHomeTransitionBoundsValid = false;
    lastPhoneSheetOffsetValid = false;
  }
  if (usedPageCache || useExplicitDirtyScan) {
    if (showGestureHint || lastShowGestureHint) overlayRegions.add(108, 211, 104, 29);
    if (displayPreferences.showFrameRate) overlayRegions.add(254, 2, 64, 24);
  }
  if (usedPageCache) {
    for (uint8_t i = 0; i < overlayRegions.count; ++i) {
      const TftRenderRect& region = overlayRegions.rects[i];
      for (int16_t row = region.y; row < region.y + region.height; ++row) {
        const size_t offset = row * AMAP_TFT_WIDTH + region.x;
        memcpy(canvas.pixels() + offset, pageCache.pixels() + offset,
               region.width * sizeof(uint16_t));
      }
    }
  }
  if (showGestureHint) {
    TftFrameRenderer::drawGestureHint(canvas, tftFont, renderedView);
  }
  if (displayPreferences.showFrameRate) {
    TftFrameRenderer::drawFrameRate(canvas, tftFont, framesPerSecond);
  }
  if (useExplicitDirtyScan) {
    for (uint8_t i = 0; i < overlayRegions.count; ++i) {
      const TftRenderRect& region = overlayRegions.rects[i];
      composeRegions.add(region.x, region.y, region.width, region.height);
    }
  }
  const uint32_t composedAt = micros();
  uint16_t* current = canvas.pixels();
  uint16_t* previous = previousFrame.pixels();
  size_t metricDirtyTileCount = 0;
  size_t metricRectangleCount = 0;
  uint32_t metricTransferPixels = 0;
  uint32_t metricTransferDurationUs = 0;
  if (!previousFrameValid) {
    queueFullFrameTransfer();
    previousFrameValid = true;
    lastPerformanceLogAt = millis();
    metricDirtyTileCount = kDirtyTileCount;
    metricRectangleCount = 1;
    metricTransferPixels = kPixels;
    Serial.printf("TFT perf: page=%s cache=no compose_regions=0 compose_pixels=%u dirty_tiles=%u/%u dirty_rects=1 pixels=%u compose=%lu ms transfer=%lu ms submit=%lu ms\n",
                  renderedViewName(renderedView), static_cast<unsigned>(kPixels),
                  static_cast<unsigned>(kDirtyTileCount),
                  static_cast<unsigned>(kDirtyTileCount), static_cast<unsigned>(kPixels),
                  static_cast<unsigned long>((composedAt - frameStartedAt) / 1000),
                  static_cast<unsigned long>(lastTransferDurationUs / 1000),
                  static_cast<unsigned long>((micros() - frameStartedAt) / 1000));
  } else {
    // Detect changes in small tiles, then merge identical horizontal runs on
    // adjacent tile rows. The third PSRAM buffer packs each merged rectangle
    // into contiguous memory, avoiding one SPI transaction per scanline.
    bool dirtyTiles[kDirtyTileRows][kDirtyTileColumns] = {};
    size_t dirtyTileCount = 0;
    for (int16_t tileY = 0; tileY < kDirtyTileRows; ++tileY) {
      for (int16_t tileX = 0; tileX < kDirtyTileColumns; ++tileX) {
        const int16_t x = tileX * kDirtyTileWidth;
        const int16_t y = tileY * kDirtyTileHeight;
        if (useExplicitDirtyScan &&
            !composeRegions.intersects(x, y, kDirtyTileWidth, kDirtyTileHeight)) {
          continue;
        }
        bool dirty = false;
        for (int16_t row = 0; row < kDirtyTileHeight && !dirty; ++row) {
          const size_t offset = (y + row) * AMAP_TFT_WIDTH + x;
          dirty = memcmp(current + offset, previous + offset,
                         kDirtyTileWidth * sizeof(uint16_t)) != 0;
        }
        dirtyTiles[tileY][tileX] = dirty;
        dirtyTileCount += dirty ? 1 : 0;
      }
    }
    metricDirtyTileCount = dirtyTileCount;

    DirtyRectangle rectangles[kMaxDirtyRectangles];
    size_t rectangleCount = 0;
    bool tooFragmented = false;
    for (int16_t tileY = 0; tileY < kDirtyTileRows && !tooFragmented; ++tileY) {
      int16_t tileX = 0;
      while (tileX < kDirtyTileColumns) {
        while (tileX < kDirtyTileColumns && !dirtyTiles[tileY][tileX]) ++tileX;
        if (tileX >= kDirtyTileColumns) break;
        const int16_t runStart = tileX;
        while (tileX < kDirtyTileColumns && dirtyTiles[tileY][tileX]) ++tileX;
        const int16_t runWidth = (tileX - runStart) * kDirtyTileWidth;
        const int16_t runX = runStart * kDirtyTileWidth;
        const int16_t runY = tileY * kDirtyTileHeight;

        bool extended = false;
        for (size_t i = 0; i < rectangleCount; ++i) {
          DirtyRectangle& rectangle = rectangles[i];
          if (rectangle.x == runX && rectangle.width == runWidth &&
              rectangle.y + rectangle.height == runY) {
            rectangle.height += kDirtyTileHeight;
            extended = true;
            break;
          }
        }
        if (!extended) {
          if (rectangleCount >= kMaxDirtyRectangles) {
            tooFragmented = true;
            break;
          }
          rectangles[rectangleCount++] = {
              runX, runY, runWidth, kDirtyTileHeight};
        }
      }
    }

    // Merge neighboring equal-height runs separated by no more than one tile.
    // Sending a few unchanged pixels is cheaper than another address-window
    // command on both the GDMA and FIFO panel paths.
    for (size_t i = 0; i < rectangleCount; ++i) {
      for (size_t j = i + 1; j < rectangleCount;) {
        DirtyRectangle& left = rectangles[i];
        const DirtyRectangle& right = rectangles[j];
        const int16_t gap = right.x - (left.x + left.width);
        if (left.y == right.y && left.height == right.height &&
            gap >= 0 && gap <= kDirtyTileWidth) {
          left.width = right.x + right.width - left.x;
          for (size_t move = j + 1; move < rectangleCount; ++move) {
            rectangles[move - 1] = rectangles[move];
          }
          --rectangleCount;
        } else {
          ++j;
        }
      }
    }

    if (tooFragmented || dirtyTileCount >= kFullRefreshDirtyTiles) {
      queueFullFrameTransfer();
      metricRectangleCount = 1;
      metricTransferPixels = kPixels;
      metricTransferDurationUs = lastTransferDurationUs;
    } else {
      // Hold CS and the SPI transaction across all dirty regions. Address
      // windows still change per rectangle, but transaction setup is paid once.
      const uint32_t transferStartedAt = micros();
      if (rectangleCount > 0) waitForFrameTransfer();
      if (rectangleCount > 0 && activePanel != nullptr) activePanel->startWrite();
      for (size_t i = 0; i < rectangleCount; ++i) {
        const DirtyRectangle& rectangle = rectangles[i];
        metricTransferPixels += static_cast<uint32_t>(rectangle.width) * rectangle.height;
        for (int16_t row = 0; row < rectangle.height; ++row) {
          const size_t sourceOffset =
              (rectangle.y + row) * AMAP_TFT_WIDTH + rectangle.x;
          memcpy(previous + sourceOffset,
                 current + sourceOffset,
                 rectangle.width * sizeof(uint16_t));
        }
        writeRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                       current + rectangle.y * AMAP_TFT_WIDTH + rectangle.x,
                       AMAP_TFT_WIDTH);
      }
      if (rectangleCount > 0 && activePanel != nullptr) activePanel->endWrite();
      metricRectangleCount = rectangleCount;
      metricTransferDurationUs = rectangleCount > 0 ? micros() - transferStartedAt : 0;
    }
    const unsigned long performanceNow = millis();
    if (performanceNow - lastPerformanceLogAt >= 1000UL) {
      lastPerformanceLogAt = performanceNow;
      Serial.printf("TFT perf: page=%s cache=%s compose_regions=%u compose_pixels=%lu dirty_tiles=%u/%u dirty_rects=%u pixels=%lu compose=%lu ms transfer=%lu ms submit=%lu ms\n",
                    renderedViewName(renderedView), usedPageCache ? "yes" : "no",
                    static_cast<unsigned>(composeRegions.count),
                    static_cast<unsigned long>(usedPageCache ? composeRegions.pixelCount() : kPixels),
                    static_cast<unsigned>(metricDirtyTileCount),
                    static_cast<unsigned>(kDirtyTileCount),
                    static_cast<unsigned>(metricRectangleCount),
                    static_cast<unsigned long>(metricTransferPixels),
                    static_cast<unsigned long>((composedAt - frameStartedAt) / 1000),
                    static_cast<unsigned long>(metricTransferDurationUs / 1000),
                    static_cast<unsigned long>((micros() - frameStartedAt) / 1000));
    }
  }
  lastFrameSignature = signature;
  lastShowGestureHint = showGestureHint;
  frameDrawn = true;
  recordRenderedFrame(millis());
  (void)ip;
  (void)port;
}

void TftRenderer::pushRectangle(int16_t x, int16_t y, int16_t width,
                                int16_t height, const uint16_t* source,
                                int16_t sourceStride) {
  if ((activePanel == nullptr && dmaPanel == nullptr) || transferBuffer == nullptr ||
      width <= 0 || height <= 0) {
    return;
  }
  if (dmaPanel != nullptr) {
    writeRectangle(x, y, width, height, source, sourceStride);
    return;
  }
  activePanel->startWrite();
  writeRectangle(x, y, width, height, source, sourceStride);
  activePanel->endWrite();
}

void TftRenderer::writeRectangle(int16_t x, int16_t y, int16_t width,
                                 int16_t height, const uint16_t* source,
                                 int16_t sourceStride) {
  const int16_t rowsPerChunk = max<int16_t>(1, kTransferPixels / width);
  if (activePanel != nullptr) activePanel->setAddrWindow(x, y, width, height);
  for (int16_t row = 0; row < height;) {
    const int16_t chunkRows = min<int16_t>(rowsPerChunk, height - row);
    for (int16_t chunkRow = 0; chunkRow < chunkRows; ++chunkRow) {
      const uint16_t* sourceRow = source + (row + chunkRow) * sourceStride;
      uint16_t* destinationRow = transferBuffer + chunkRow * width;
      memcpy(destinationRow, sourceRow, width * sizeof(uint16_t));
    }
    if (dmaPanel != nullptr) {
      while (xSemaphoreTake(dmaTransferDone, 0) == pdTRUE) {}
      const esp_err_t result = esp_lcd_panel_draw_bitmap(
          dmaPanel, x, y + row, x + width, y + row + chunkRows, transferBuffer);
      if (result != ESP_OK ||
          xSemaphoreTake(dmaTransferDone, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.printf("TFT DMA transfer failed: %s\n", esp_err_to_name(result));
        return;
      }
    } else {
      activePanel->writePixels(transferBuffer,
                               static_cast<uint32_t>(chunkRows) * width,
                               true, true);
    }
    row += chunkRows;
  }
}

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct WeatherForecastDay {
  String date;
  int weatherCode = -1;
  float highC = NAN;
  float lowC = NAN;
  int rainChance = -1;
};

// The UI only reads snapshots. Network I/O happens in a low-priority worker so
// a slow weather provider never freezes touch input, navigation, or rendering.
struct WeatherState {
  bool configured = false;
  bool loading = false;
  bool valid = false;
  String city;
  String timezone;
  String condition;
  String error;
  float temperatureC = NAN;
  float feelsLikeC = NAN;
  float windKph = NAN;
  int humidity = -1;
  int weatherCode = -1;
  bool airQualityValid = false;
  int usAqi = -1;
  float pm25 = NAN;
  float pm10 = NAN;
  String airQualityError;
  bool isDay = true;
  unsigned long updatedAt = 0;
  WeatherForecastDay days[3];
};

class WeatherService {
public:
  void begin();
  void update(bool wifiConnected);
  WeatherState snapshot() const;
  String cityName() const;
  bool setCityName(String city);

private:
  mutable SemaphoreHandle_t lock = nullptr;
  WeatherState state;
  float latitude = NAN;
  float longitude = NAN;
  bool requestInFlight = false;
  uint32_t settingsGeneration = 0;
  unsigned long nextRefreshAt = 0;

  static void taskEntry(void* context);
  void fetch();
  void loadSettings();
  void saveSettings();
  bool resolveCity(const String& city, float& resolvedLatitude, float& resolvedLongitude,
                   String& resolvedName, String& resolvedTimezone, String& error);
  bool fetchForecast(float latitude, float longitude, WeatherState& result, String& error);
  bool fetchAirQuality(float latitude, float longitude, WeatherState& result, String& error);
  static String urlEncode(const String& value);
  static String conditionForCode(int code);
};

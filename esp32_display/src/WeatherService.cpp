#include "WeatherService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include <math.h>

namespace {
constexpr const char* kNamespace = "amap_weather";
constexpr const char* kCityKey = "city";
constexpr const char* kLatitudeKey = "lat";
constexpr const char* kLongitudeKey = "lon";
constexpr unsigned long kRefreshMs = 30UL * 60UL * 1000UL;
constexpr unsigned long kRetryMs = 5UL * 60UL * 1000UL;
constexpr unsigned long kRequestTimeoutMs = 6500UL;

bool requestJson(const String& url, String& payload, String& error) {
  WiFiClientSecure client;
  // Open-Meteo does not require a user token. TLS still encrypts the location
  // request; certificate validation is unavailable in the small Arduino core
  // bundle, so keep this limited to the fixed HTTPS provider hosts below.
  client.setInsecure();
  client.setTimeout(kRequestTimeoutMs / 1000U);
  HTTPClient http;
  http.setConnectTimeout(kRequestTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);
  if (!http.begin(client, url)) {
    error = "天气服务连接初始化失败";
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = String("天气服务响应 ") + code;
    http.end();
    return false;
  }
  payload = http.getString();
  http.end();
  if (payload.isEmpty()) {
    error = "天气服务返回空数据";
    return false;
  }
  return true;
}
}  // namespace

void WeatherService::begin() {
  lock = xSemaphoreCreateMutex();
  if (lock == nullptr) {
    Serial.println("Weather service: mutex allocation failed");
    return;
  }
  loadSettings();
}

void WeatherService::loadSettings() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  state.city = prefs.getString(kCityKey, "");
  latitude = prefs.getFloat(kLatitudeKey, NAN);
  longitude = prefs.getFloat(kLongitudeKey, NAN);
  prefs.end();
  state.configured = !state.city.isEmpty();
}

void WeatherService::saveSettings() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putString(kCityKey, state.city);
  if (isnan(latitude) || isnan(longitude)) {
    prefs.remove(kLatitudeKey);
    prefs.remove(kLongitudeKey);
  } else {
    prefs.putFloat(kLatitudeKey, latitude);
    prefs.putFloat(kLongitudeKey, longitude);
  }
  prefs.end();
}

WeatherState WeatherService::snapshot() const {
  WeatherState copy;
  if (lock == nullptr) return copy;
  xSemaphoreTake(lock, portMAX_DELAY);
  copy = state;
  xSemaphoreGive(lock);
  return copy;
}

String WeatherService::cityName() const {
  if (lock == nullptr) return String();
  xSemaphoreTake(lock, portMAX_DELAY);
  const String city = state.city;
  xSemaphoreGive(lock);
  return city;
}

bool WeatherService::setCityName(String city) {
  city.trim();
  if (city.length() < 2 || city.length() > 72 || lock == nullptr) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  state = WeatherState{};
  state.city = city;
  state.configured = true;
  latitude = NAN;
  longitude = NAN;
  ++settingsGeneration;
  nextRefreshAt = 0;
  saveSettings();
  xSemaphoreGive(lock);
  Serial.printf("Weather city saved: %s\n", city.c_str());
  return true;
}

void WeatherService::update(bool wifiConnected) {
  if (!wifiConnected || lock == nullptr) return;
  const unsigned long now = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool shouldFetch = state.configured && !requestInFlight &&
                           static_cast<long>(now - nextRefreshAt) >= 0;
  if (shouldFetch) {
    requestInFlight = true;
    state.loading = true;
    state.error = "";
  }
  xSemaphoreGive(lock);
  if (!shouldFetch) return;

  TaskHandle_t handle = nullptr;
  if (xTaskCreate(taskEntry, "weather_fetch", 7168, this, 1, &handle) != pdPASS) {
    xSemaphoreTake(lock, portMAX_DELAY);
    requestInFlight = false;
    state.loading = false;
    state.error = "无法启动天气后台任务";
    nextRefreshAt = now + kRetryMs;
    xSemaphoreGive(lock);
  }
}

bool WeatherService::requestRefresh() {
  if (lock == nullptr) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool accepted = state.configured && !requestInFlight;
  if (accepted) nextRefreshAt = 0;
  xSemaphoreGive(lock);
  return accepted;
}

void WeatherService::taskEntry(void* context) {
  static_cast<WeatherService*>(context)->fetch();
  vTaskDelete(nullptr);
}

void WeatherService::fetch() {
  String city;
  float savedLatitude = NAN;
  float savedLongitude = NAN;
  uint32_t generation = 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  city = state.city;
  savedLatitude = latitude;
  savedLongitude = longitude;
  generation = settingsGeneration;
  xSemaphoreGive(lock);

  WeatherState result;
  result.city = city;
  result.configured = !city.isEmpty();
  String error;
  String resolvedName = city;
  String resolvedTimezone;
  if (isnan(savedLatitude) || isnan(savedLongitude)) {
    if (!resolveCity(city, savedLatitude, savedLongitude, resolvedName, resolvedTimezone, error)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      if (generation == settingsGeneration) {
        state.loading = false;
        state.error = error;
        nextRefreshAt = millis() + kRetryMs;
      }
      requestInFlight = false;
      xSemaphoreGive(lock);
      Serial.printf("Weather city lookup failed: %s\n", error.c_str());
      return;
    }
  }
  if (!fetchForecast(savedLatitude, savedLongitude, result, error)) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (generation == settingsGeneration) {
      state.loading = false;
      state.error = error;
      nextRefreshAt = millis() + kRetryMs;
    }
    requestInFlight = false;
    xSemaphoreGive(lock);
    Serial.printf("Weather forecast failed: %s\n", error.c_str());
    return;
  }
  String airQualityError;
  if (!fetchAirQuality(savedLatitude, savedLongitude, result, airQualityError)) {
    // Forecast and air quality are independent products. Keep a successful
    // weather screen usable when the lower-frequency air endpoint is late.
    result.airQualityValid = false;
    result.airQualityError = airQualityError;
  }
  result.city = resolvedName;
  if (!resolvedTimezone.isEmpty()) result.timezone = resolvedTimezone;
  result.valid = true;
  result.updatedAt = millis();

  xSemaphoreTake(lock, portMAX_DELAY);
  if (generation == settingsGeneration) {
    state = result;
    latitude = savedLatitude;
    longitude = savedLongitude;
    saveSettings();
    nextRefreshAt = millis() + kRefreshMs;
  }
  requestInFlight = false;
  xSemaphoreGive(lock);
  Serial.printf("Weather updated: %s %.1fC\n", result.city.c_str(), result.temperatureC);
}

bool WeatherService::resolveCity(const String& city, float& resolvedLatitude, float& resolvedLongitude,
                                 String& resolvedName, String& resolvedTimezone, String& error) {
  String payload;
  if (!requestJson("https://geocoding-api.open-meteo.com/v1/search?count=1&language=zh&name=" +
                   urlEncode(city), payload, error)) return false;
  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(doc, payload);
  if (parseError || !doc["results"].is<JsonArray>() || doc["results"].size() == 0) {
    error = "未找到该城市，请检查名称";
    return false;
  }
  JsonObject place = doc["results"][0];
  resolvedLatitude = place["latitude"] | NAN;
  resolvedLongitude = place["longitude"] | NAN;
  if (isnan(resolvedLatitude) || isnan(resolvedLongitude)) {
    error = "城市坐标无效";
    return false;
  }
  resolvedName = String(place["name"] | city.c_str());
  resolvedTimezone = String(place["timezone"] | "");
  return true;
}

bool WeatherService::fetchForecast(float latitude, float longitude, WeatherState& result,
                                   String& error) {
  const String url = "https://api.open-meteo.com/v1/forecast?latitude=" +
      String(latitude, 5) + "&longitude=" + String(longitude, 5) +
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
      "&timezone=auto&forecast_days=3";
  String payload;
  if (!requestJson(url, payload, error)) return false;
  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(doc, payload);
  if (parseError || !doc["current"].is<JsonObject>() || !doc["daily"].is<JsonObject>()) {
    error = "天气数据格式无效";
    return false;
  }
  JsonObject current = doc["current"];
  result.temperatureC = current["temperature_2m"] | NAN;
  result.feelsLikeC = current["apparent_temperature"] | NAN;
  result.humidity = current["relative_humidity_2m"] | -1;
  result.weatherCode = current["weather_code"] | -1;
  result.windKph = current["wind_speed_10m"] | NAN;
  result.isDay = (current["is_day"] | 1) == 1;
  result.condition = conditionForCode(result.weatherCode);
  result.timezone = String(doc["timezone"] | "");
  JsonObject daily = doc["daily"];
  for (uint8_t i = 0; i < 3; ++i) {
    result.days[i].date = String(daily["time"][i] | "");
    result.days[i].weatherCode = daily["weather_code"][i] | -1;
    result.days[i].highC = daily["temperature_2m_max"][i] | NAN;
    result.days[i].lowC = daily["temperature_2m_min"][i] | NAN;
    result.days[i].rainChance = daily["precipitation_probability_max"][i] | -1;
  }
  return !isnan(result.temperatureC);
}

bool WeatherService::fetchAirQuality(float latitude, float longitude, WeatherState& result,
                                     String& error) {
  const String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" +
      String(latitude, 5) + "&longitude=" + String(longitude, 5) +
      "&current=us_aqi,pm2_5,pm10&timezone=auto";
  String payload;
  if (!requestJson(url, payload, error)) return false;
  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(doc, payload);
  if (parseError || !doc["current"].is<JsonObject>()) {
    error = "空气质量数据格式无效";
    return false;
  }
  JsonObject current = doc["current"];
  result.usAqi = current["us_aqi"] | -1;
  result.pm25 = current["pm2_5"] | NAN;
  result.pm10 = current["pm10"] | NAN;
  result.airQualityValid = result.usAqi >= 0 || !isnan(result.pm25);
  return result.airQualityValid;
}

String WeatherService::urlEncode(const String& value) {
  String encoded;
  char escaped[4];
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      snprintf(escaped, sizeof(escaped), "%%%02X", c);
      encoded += escaped;
    }
  }
  return encoded;
}

String WeatherService::conditionForCode(int code) {
  if (code == 0) return "晴朗";
  if (code <= 2) return "少云";
  if (code == 3) return "阴";
  if (code == 45 || code == 48) return "雾";
  if (code <= 57) return "毛毛雨";
  if (code <= 67) return "降雨";
  if (code <= 77) return "降雪";
  if (code <= 82) return "阵雨";
  if (code <= 86) return "阵雪";
  if (code >= 95) return "雷暴";
  return "天气待更新";
}

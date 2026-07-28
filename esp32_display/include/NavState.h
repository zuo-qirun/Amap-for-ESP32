#pragma once

#include <Arduino.h>

struct TurnState {
  int icon = 0;
  String text;
  String distanceText;
  int distanceMeters = -1;
  String road;
};

struct EtaState {
  String remainDistanceText;
  String remainTimeText;
  String arriveTimeText;
};

struct SpeedState {
  int current = -1;
  int limit = -1;
  int overspeedLevel = 0;
};

struct LaneState {
  static const uint8_t MAX_LANES = 8;
  int lanes[MAX_LANES] = {0};
  bool advised[MAX_LANES] = {false};
  uint8_t count = 0;

  void clear();
};

struct LightState {
  int dir = -1;
  int status = -1;
  int seconds = 0;
};

struct CameraState {
  int type = -1;
  int distance = -1;
  int speedLimit = -1;
};

struct TmcState {
  static const uint8_t MAX_SEGMENTS = 8;
  int totalDistance = -1;
  int finishDistance = -1;
  int status[MAX_SEGMENTS] = {0};
  int distance[MAX_SEGMENTS] = {0};
  uint8_t count = 0;

  void clear();
};

// These protocol fields are intentionally display-driver agnostic. The OLED
// uses them as rotating secondary text today; a future SPI TFT can render the
// same structured data in a denser dashboard without changing the phone link.
struct RouteState {
  int remainingMeters = -1;
  int totalMeters = -1;
  int remainingSeconds = -1;
  int progressPercent = -1;
  String destination;
  int remainingTrafficLights = -1;
};

struct RoadInfoState {
  String type;
  int bearing = -1;
  String traffic;
  bool crossMap = false;
};

struct GuideInfoState {
  String exitName;
  String exitDirection;
  String serviceAreaName;
  String serviceAreaDistance;
  String nextServiceAreaName;
  String nextServiceAreaDistance;
};

struct MusicState {
  bool active = false;
  bool playing = false;
  String source;
  String sourceName;
  int64_t songId = -1;
  String title;
  String artist;
  String album;
  String coverUrl;
  int64_t positionMs = 0;
  int64_t durationMs = -1;
  String previousLyric;
  String lyric;
  String translatedLyric;
  String nextLyric;
  // Refined Now Playing treats a long lyric gap as a synthetic empty line and
  // animates three timed dots instead of showing a stale lyric.
  bool interlude = false;
  String highlightedLyric;
  String currentWord;
  int64_t lineStartMs = -1;
  int64_t lineDurationMs = 0;
  int64_t wordStartMs = -1;
  int64_t wordDurationMs = 0;
  int wordProgressPermille = 0;
  unsigned long receivedAt = 0;

  int64_t positionAt(unsigned long now) const;
  int wordProgressAt(unsigned long now) const;
};

struct PhoneNotificationState {
  bool active = false;
  String kind;
  String app;
  String sender;
  String title;
  String body;
  int64_t postedAt = 0;
  int64_t expiresAt = 0;
};

struct PhoneCalendarState {
  String title;
  String location;
  int64_t startAt = -1;
  int64_t endAt = -1;
};

struct PhoneWeatherState {
  String provider;
  String condition;
  int code = -1;
  float temperatureC = NAN;
  float precipitationMm = NAN;
  int aqi = -1;
  String alert;
  int64_t observedAt = 0;
  String error;
};

struct PhoneDeviceState {
  int batteryPercent = -1;
  bool charging = false;
  String network = "none";
  bool bluetoothOn = false;
  int signalLevel = -1;
};

struct PhoneState {
  bool enabled = false;
  PhoneNotificationState notification;
  PhoneCalendarState calendar;
  PhoneWeatherState weather;
  PhoneDeviceState device;
  unsigned long receivedAt = 0;
};

struct NavState {
  static const uint8_t MAX_LIGHTS = 4;

  bool active = false;
  String mode = "standby";
  String road;
  String heading;
  TurnState turn;
  EtaState eta;
  SpeedState speed;
  LaneState lane;
  LightState lights[MAX_LIGHTS];
  uint8_t lightCount = 0;
  CameraState camera;
  TmcState tmc;
  RouteState route;
  RoadInfoState roadInfo;
  GuideInfoState guide;
  MusicState music;
  PhoneState phone;
  String alert;
  String detail;
  uint32_t seq = 0;
  unsigned long lastPacketAt = 0;

  void reset();
};

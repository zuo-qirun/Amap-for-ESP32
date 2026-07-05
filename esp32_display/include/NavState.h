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
  String alert;
  String detail;
  uint32_t seq = 0;
  unsigned long lastPacketAt = 0;

  void reset();
};

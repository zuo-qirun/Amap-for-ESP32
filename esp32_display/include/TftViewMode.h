#pragma once

#include <Arduino.h>

enum class TftViewMode : uint8_t {
  Home = 0,
  Auto = 1,
  Navigation = 2,
  Music = 3,
  Settings = 4,
  AutoStatus = 5,
  Weather = 6,
};

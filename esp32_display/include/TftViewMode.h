#pragma once

#include <Arduino.h>

enum class TftViewMode : uint8_t {
  Auto = 0,
  Navigation = 1,
  Music = 2,
  Status = 3,
};

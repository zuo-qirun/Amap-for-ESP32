#pragma once

#include <Arduino.h>
#include "NavState.h"

class ProtocolParser {
public:
  bool parse(const char* payload, size_t length, NavState& target, String& error);

private:
  static String readText(const char* value, const char* fallback = "");
  static String limitText(const String& value, size_t maxBytes);
};

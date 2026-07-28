#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "NavState.h"

class ProtocolParser {
public:
  bool parse(const char* payload, size_t length, NavState& target, String& error);
  bool musicAccepted() const { return musicAcceptedLastParse; }

private:
  bool musicAcceptedLastParse = false;
  static String readText(const char* value, const char* fallback = "");
  static String limitText(const String& value, size_t maxBytes);
  static bool parseMusic(JsonObject music, MusicState& target, unsigned long now);
  static void parsePhone(JsonObject phone, PhoneState& target);
};

#pragma once

#include <Arduino.h>

enum class MediaControlCommand : uint8_t {
  None = 0,
  Previous,
  PlayPause,
  Next,
};

inline const char* mediaControlAction(MediaControlCommand command) {
  switch (command) {
    case MediaControlCommand::Previous: return "previous";
    case MediaControlCommand::PlayPause: return "play_pause";
    case MediaControlCommand::Next: return "next";
    default: return "";
  }
}

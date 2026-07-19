#include "NavState.h"

int64_t MusicState::positionAt(unsigned long now) const {
  int64_t result = positionMs;
  if (playing && receivedAt != 0) {
    result += static_cast<unsigned long>(now - receivedAt);
  }
  if (durationMs > 0) result = min<int64_t>(result, durationMs);
  return max<int64_t>(0, result);
}

int MusicState::wordProgressAt(unsigned long now) const {
  if (wordStartMs >= 0 && wordDurationMs > 0) {
    const int64_t elapsed = positionAt(now) - wordStartMs;
    return constrain(static_cast<int>(elapsed * 1000 / wordDurationMs), 0, 1000);
  }
  return constrain(wordProgressPermille, 0, 1000);
}

void LaneState::clear() {
  count = 0;
  for (uint8_t i = 0; i < MAX_LANES; ++i) {
    lanes[i] = 0;
    advised[i] = false;
  }
}

void TmcState::clear() {
  totalDistance = -1;
  finishDistance = -1;
  count = 0;
  for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) {
    status[i] = 0;
    distance[i] = 0;
  }
}

void NavState::reset() {
  active = false;
  mode = "standby";
  road = "";
  heading = "";
  turn = TurnState();
  eta = EtaState();
  speed = SpeedState();
  lane.clear();
  lightCount = 0;
  camera = CameraState();
  tmc.clear();
  route = RouteState();
  roadInfo = RoadInfoState();
  guide = GuideInfoState();
  music = MusicState();
  alert = "";
  detail = "";
  seq = 0;
  lastPacketAt = 0;
}

#include "NavState.h"

void LaneState::clear() {
  count = 0;
  for (uint8_t i = 0; i < MAX_LANES; ++i) {
    lanes[i] = 0;
    advised[i] = false;
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
  alert = "";
  detail = "";
  seq = 0;
  lastPacketAt = 0;
}

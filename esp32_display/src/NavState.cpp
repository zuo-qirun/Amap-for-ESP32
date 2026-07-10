#include "NavState.h"

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
  alert = "";
  detail = "";
  seq = 0;
  lastPacketAt = 0;
}

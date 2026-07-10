#include "ProtocolParser.h"

#include <ArduinoJson.h>

bool ProtocolParser::parse(const char* payload, size_t length, NavState& target, String& error) {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, payload, length);
  if (jsonError) {
    error = jsonError.c_str();
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  int proto = root["proto"] | 0;
  const char* type = root["type"] | "";
  if (proto != 1 || strcmp(type, "nav_state") != 0) {
    error = "unsupported proto/type";
    return false;
  }

  target.seq = root["seq"] | target.seq;
  target.active = root["active"] | false;
  target.mode = limitText(readText(root["mode"] | "standby"), 16);
  target.road = limitText(readText(root["road"] | ""), 48);
  target.heading = limitText(readText(root["heading"] | ""), 16);

  JsonObject turn = root["turn"].as<JsonObject>();
  target.turn.icon = turn["icon"] | 0;
  target.turn.text = limitText(readText(turn["text"] | ""), 24);
  target.turn.distanceText = limitText(readText(turn["distanceText"] | ""), 24);
  target.turn.distanceMeters = turn["distanceMeters"] | -1;
  target.turn.road = limitText(readText(turn["road"] | ""), 48);

  JsonObject eta = root["eta"].as<JsonObject>();
  target.eta.remainDistanceText = limitText(readText(eta["remainDistanceText"] | ""), 24);
  target.eta.remainTimeText = limitText(readText(eta["remainTimeText"] | ""), 24);
  target.eta.arriveTimeText = limitText(readText(eta["arriveTimeText"] | ""), 24);

  JsonObject speed = root["speed"].as<JsonObject>();
  target.speed.current = speed["current"] | -1;
  target.speed.limit = speed["limit"] | -1;
  target.speed.overspeedLevel = speed["overspeedLevel"] | 0;

  target.lane.clear();
  JsonObject lane = root["lane"].as<JsonObject>();
  JsonArray lanes = lane["lanes"].as<JsonArray>();
  JsonArray advised = lane["advised"].as<JsonArray>();
  for (JsonVariant value : lanes) {
    if (target.lane.count >= LaneState::MAX_LANES) {
      break;
    }
    uint8_t index = target.lane.count++;
    target.lane.lanes[index] = value | 0;
    target.lane.advised[index] = advised[index] | false;
  }

  target.lightCount = 0;
  JsonArray lights = root["lights"].as<JsonArray>();
  for (JsonObject light : lights) {
    if (target.lightCount >= NavState::MAX_LIGHTS) {
      break;
    }
    LightState& out = target.lights[target.lightCount++];
    out.dir = light["dir"] | -1;
    out.status = light["status"] | -1;
    out.seconds = light["seconds"] | 0;
  }

  JsonObject camera = root["camera"].as<JsonObject>();
  target.camera.type = camera["type"] | -1;
  target.camera.distance = camera["distance"] | -1;
  target.camera.speedLimit = camera["speedLimit"] | -1;

  target.tmc.clear();
  JsonObject tmc = root["tmc"].as<JsonObject>();
  target.tmc.totalDistance = tmc["totalDistance"] | -1;
  target.tmc.finishDistance = tmc["finishDistance"] | -1;
  JsonArray tmcSegments = tmc["segments"].as<JsonArray>();
  for (JsonObject segment : tmcSegments) {
    if (target.tmc.count >= TmcState::MAX_SEGMENTS) {
      break;
    }
    const int distance = segment["distance"] | 0;
    if (distance <= 0) {
      continue;
    }
    const uint8_t index = target.tmc.count++;
    target.tmc.status[index] = segment["status"] | 0;
    target.tmc.distance[index] = distance;
  }
  if (target.tmc.count == 0 || target.tmc.totalDistance <= 0) {
    target.tmc.clear();
  } else if (target.tmc.finishDistance < 0) {
    target.tmc.finishDistance = 0;
  } else if (target.tmc.finishDistance > target.tmc.totalDistance) {
    target.tmc.finishDistance = target.tmc.totalDistance;
  }

  JsonObject route = root["route"].as<JsonObject>();
  target.route.remainingMeters = route["remainingMeters"] | -1;
  target.route.totalMeters = route["totalMeters"] | -1;
  target.route.remainingSeconds = route["remainingSeconds"] | -1;
  target.route.progressPercent = route["progressPercent"] | -1;
  target.route.destination = limitText(readText(route["destination"] | ""), 48);
  target.route.remainingTrafficLights = route["remainingTrafficLights"] | -1;

  JsonObject roadInfo = root["roadInfo"].as<JsonObject>();
  target.roadInfo.type = limitText(readText(roadInfo["type"] | ""), 24);
  target.roadInfo.bearing = roadInfo["bearing"] | -1;
  target.roadInfo.traffic = limitText(readText(roadInfo["traffic"] | ""), 48);
  target.roadInfo.crossMap = roadInfo["crossMap"] | false;

  JsonObject guide = root["guide"].as<JsonObject>();
  target.guide.exitName = limitText(readText(guide["exitName"] | ""), 48);
  target.guide.exitDirection = limitText(readText(guide["exitDirection"] | ""), 32);
  target.guide.serviceAreaName = limitText(readText(guide["serviceAreaName"] | ""), 48);
  target.guide.serviceAreaDistance = limitText(readText(guide["serviceAreaDistance"] | ""), 24);
  target.guide.nextServiceAreaName = limitText(readText(guide["nextServiceAreaName"] | ""), 48);
  target.guide.nextServiceAreaDistance =
      limitText(readText(guide["nextServiceAreaDistance"] | ""), 24);
  target.alert = limitText(readText(root["alert"] | ""), 72);
  target.detail = limitText(readText(root["detail"] | ""), 120);
  target.lastPacketAt = millis();
  error = "";
  return true;
}

String ProtocolParser::readText(const char* value, const char* fallback) {
  if (value == nullptr) {
    return String(fallback);
  }
  return String(value);
}

String ProtocolParser::limitText(const String& value, size_t maxBytes) {
  if (value.length() <= maxBytes) {
    return value;
  }
  String out = value.substring(0, maxBytes);
  while (out.length() > 0 && (static_cast<uint8_t>(out[out.length() - 1]) & 0xC0) == 0x80) {
    out.remove(out.length() - 1);
  }
  return out;
}

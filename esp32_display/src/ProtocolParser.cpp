#include "ProtocolParser.h"

#include <ArduinoJson.h>

namespace {
constexpr unsigned long kActiveMusicSourceHoldMs = 1500UL;

uint8_t musicSourcePriority(const String& source) {
  // Native providers carry track identity, cover, translation and word timing;
  // generic MediaSession remains the automatic fallback after they go quiet.
  if (source == "netease") return 2;
  return source.isEmpty() ? 0 : 1;
}
}

bool ProtocolParser::parse(const char* payload, size_t length, NavState& target, String& error) {
  musicAcceptedLastParse = false;
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, payload, length);
  if (jsonError) {
    error = jsonError.c_str();
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  int proto = root["proto"] | 0;
  const char* type = root["type"] | "";
  const bool musicUpdate = strcmp(type, "music_update") == 0;
  const bool phoneUpdate = strcmp(type, "phone_update") == 0;
  if (proto != 1 || (!musicUpdate && !phoneUpdate && strcmp(type, "nav_state") != 0)) {
    error = "unsupported proto/type";
    return false;
  }

  target.seq = root["seq"] | target.seq;
  if (phoneUpdate) {
    parsePhone(root["phone"].as<JsonObject>(), target.phone);
    target.lastPacketAt = millis();
    error = "";
    return true;
  }
  if (musicUpdate) {
    target.active = root["active"] | target.active;
    target.mode = limitText(readText(root["mode"] | target.mode.c_str()), 16);
    const unsigned long now = millis();
    musicAcceptedLastParse = parseMusic(root["music"].as<JsonObject>(), target.music, now);
    target.lastPacketAt = now;
    error = "";
    return true;
  }
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

  const unsigned long now = millis();
  musicAcceptedLastParse = parseMusic(root["music"].as<JsonObject>(), target.music, now);
  parsePhone(root["phone"].as<JsonObject>(), target.phone);
  target.alert = limitText(readText(root["alert"] | ""), 72);
  target.detail = limitText(readText(root["detail"] | ""), 120);
  target.lastPacketAt = now;
  error = "";
  return true;
}

bool ProtocolParser::parseMusic(JsonObject music, MusicState& target, unsigned long now) {
  if (music.isNull()) return false;

  const bool incomingActive = music["active"] | false;
  const String incomingSource = limitText(readText(music["source"] | ""), 16);
  const bool currentSourceFresh =
      target.active && target.receivedAt != 0 && now - target.receivedAt <= kActiveMusicSourceHoldMs;
  const bool differentKnownSource =
      !incomingSource.isEmpty() && !target.source.isEmpty() && incomingSource != target.source;

  // Multiple transports can be connected at once. An inactive heartbeat from
  // one source must not blank a currently active source that is still sending.
  // While a native provider is fresh, a lower-priority generic MediaSession
  // frame must not replace its richer lyrics and cover on every BLE poll.
  // A silent source expires naturally after the short hold window.
  if (!incomingActive && currentSourceFresh && differentKnownSource) return false;
  if (incomingActive && currentSourceFresh && differentKnownSource &&
      musicSourcePriority(incomingSource) < musicSourcePriority(target.source)) {
    return false;
  }

  target.active = incomingActive;
  target.playing = music["playing"] | false;
  target.source = incomingSource;
  target.sourceName = limitText(readText(music["sourceName"] | "音乐播放器"), 48);
  target.songId = music["songId"] | static_cast<int64_t>(-1);
  target.title = limitText(readText(music["title"] | ""), 96);
  target.artist = limitText(readText(music["artist"] | ""), 96);
  target.album = limitText(readText(music["album"] | ""), 96);
  target.coverUrl = limitText(readText(music["coverUrl"] | ""), 512);
  target.positionMs = music["positionMs"] | static_cast<int64_t>(0);
  target.durationMs = music["durationMs"] | static_cast<int64_t>(-1);
  target.previousLyric = limitText(readText(music["previousLyric"] | ""), 240);
  target.lyric = limitText(readText(music["lyric"] | ""), 240);
  target.translatedLyric = limitText(readText(music["translatedLyric"] | ""), 240);
  target.nextLyric = limitText(readText(music["nextLyric"] | ""), 240);
  target.interlude = music["interlude"] | false;
  target.highlightedLyric = limitText(readText(music["highlightedLyric"] | ""), 240);
  target.currentWord = limitText(readText(music["currentWord"] | ""), 72);
  target.lineStartMs = music["lineStartMs"] | static_cast<int64_t>(-1);
  target.lineDurationMs = music["lineDurationMs"] | static_cast<int64_t>(0);
  target.wordStartMs = music["wordStartMs"] | static_cast<int64_t>(-1);
  target.wordDurationMs = music["wordDurationMs"] | static_cast<int64_t>(0);
  target.wordProgressPermille = constrain(music["wordProgressPermille"] | 0, 0, 1000);
  target.receivedAt = now;
  return true;
}

void ProtocolParser::parsePhone(JsonObject phone, PhoneState& target) {
  if (phone.isNull()) return;
  target.enabled = phone["enabled"] | false;
  JsonObject notification = phone["notification"].as<JsonObject>();
  target.notification.active = notification["active"] | false;
  target.notification.kind = limitText(readText(notification["kind"] | ""), 16);
  target.notification.app = limitText(readText(notification["app"] | ""), 48);
  target.notification.sender = limitText(readText(notification["sender"] | ""), 128);
  target.notification.title = limitText(readText(notification["title"] | ""), 192);
  target.notification.body = limitText(readText(notification["body"] | ""), 1536);
  target.notification.postedAt = notification["postedAt"] | static_cast<int64_t>(0);
  target.notification.expiresAt = notification["expiresAt"] | static_cast<int64_t>(0);
  JsonObject calendar = phone["calendar"].as<JsonObject>();
  target.calendar.title = limitText(readText(calendar["title"] | ""), 192);
  target.calendar.location = limitText(readText(calendar["location"] | ""), 192);
  target.calendar.startAt = calendar["startAt"] | static_cast<int64_t>(-1);
  target.calendar.endAt = calendar["endAt"] | static_cast<int64_t>(-1);
  JsonObject weather = phone["weather"].as<JsonObject>();
  target.weather.provider = limitText(readText(weather["provider"] | ""), 32);
  target.weather.condition = limitText(readText(weather["condition"] | ""), 96);
  target.weather.code = weather["code"] | -1;
  target.weather.temperatureC = weather["temperatureC"] | NAN;
  target.weather.precipitationMm = weather["precipitationMm"] | NAN;
  target.weather.aqi = weather["aqi"] | -1;
  target.weather.alert = limitText(readText(weather["alert"] | ""), 192);
  target.weather.observedAt = weather["observedAt"] | static_cast<int64_t>(0);
  target.weather.error = limitText(readText(weather["error"] | ""), 128);
  JsonObject device = phone["device"].as<JsonObject>();
  target.device.batteryPercent = device["batteryPercent"] | -1;
  target.device.charging = device["charging"] | false;
  target.device.network = limitText(readText(device["network"] | "none"), 16);
  target.device.bluetoothOn = device["bluetoothOn"] | false;
  target.device.signalLevel = device["signalLevel"] | -1;
  target.receivedAt = millis();
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
  size_t end = 0;
  while (end < value.length()) {
    const uint8_t first = static_cast<uint8_t>(value[end]);
    size_t bytes = 1;
    if ((first & 0xE0) == 0xC0) bytes = 2;
    else if ((first & 0xF0) == 0xE0) bytes = 3;
    else if ((first & 0xF8) == 0xF0) bytes = 4;
    if (end + bytes > maxBytes || end + bytes > value.length()) break;
    bool valid = bytes == 1 || (first & 0xC0) != 0x80;
    for (size_t index = 1; index < bytes && valid; ++index) {
      valid = (static_cast<uint8_t>(value[end + index]) & 0xC0) == 0x80;
    }
    end += valid ? bytes : 1;
  }
  return value.substring(0, end);
}

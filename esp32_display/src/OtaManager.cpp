#include "OtaManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "Config.h"

namespace {
const char* OTA_PREF_NAMESPACE = "amap_ota";
const char* OTA_PREF_HEALTHY = "healthy";
const char* OTA_PREF_BOOT_ATTEMPTS = "boot_attempts";
const char* OTA_PREF_VERSION = "version";
const char* OTA_PREF_BUILD = "build";
const char* OTA_PREF_CHANNEL = "channel";

String hexByte(uint8_t value) {
  const char* digits = "0123456789abcdef";
  String out;
  out += digits[value >> 4];
  out += digits[value & 0x0F];
  return out;
}
}  // namespace

void OtaManager::begin() {
  bootStartedAt = millis();
  noteBoot();
}

void OtaManager::update(bool networkConnected, bool webReady, bool oledReady) {
  unsigned long now = millis();
  if (shouldAttemptDevFallback(networkConnected, webReady, oledReady)) {
    fallbackAttempted = true;
    fallbackToStableNow();
    return;
  }

  if (!healthyMarked && networkConnected && webReady && oledReady &&
      now - bootStartedAt >= OTA_HEALTHY_MARK_DELAY_MS) {
    markHealthy();
  }

  if (networkConnected && !isBusy() &&
      (lastCheckAt == 0 || now - lastCheckAt >= OTA_CHECK_INTERVAL_MS)) {
    checkNow();
  }
}

bool OtaManager::checkNow() {
  if (isBusy()) {
    lastError = "OTA 正忙，请稍后再试";
    return false;
  }
  state = State::Checking;
  lastError = "";
  lastCheckAt = millis();

  OtaManifest manifest;
  String error;
  if (!fetchManifest(OTA_CHANNEL, manifest, error)) {
    lastError = error;
    state = State::Error;
    return false;
  }

  latest = manifest;
  state = isManifestNewer(latest) ? State::UpdateAvailable : State::UpToDate;
  return true;
}

bool OtaManager::upgradeNow() {
  if (isBusy()) {
    lastError = "OTA 正忙，请稍后再试";
    return false;
  }
  if (latest.firmwareUrl.isEmpty()) {
    if (!checkNow()) {
      return false;
    }
  }
  if (!isManifestNewer(latest)) {
    state = State::UpToDate;
    return true;
  }

  return installManifest(latest, lastError);
}

bool OtaManager::fallbackToStableNow() {
  if (isBusy()) {
    return false;
  }
  state = State::Checking;
  lastError = "";

  OtaManifest stable;
  String error;
  if (!fetchManifest("stable", stable, error)) {
    lastError = "stable 回退检查失败：" + error;
    state = State::Error;
    return false;
  }
  return installManifest(stable, lastError);
}

bool OtaManager::updateAvailable() const {
  return state == State::UpdateAvailable && isManifestNewer(latest);
}

bool OtaManager::isBusy() const {
  return state == State::Checking || state == State::Upgrading;
}

String OtaManager::statusText() const {
  return stateName();
}

String OtaManager::lastErrorText() const {
  return lastError;
}

String OtaManager::currentVersion() const {
  return AMAP_FIRMWARE_VERSION;
}

uint32_t OtaManager::currentBuild() const {
  return static_cast<uint32_t>(AMAP_FIRMWARE_BUILD);
}

String OtaManager::currentChannel() const {
  return OTA_CHANNEL;
}

String OtaManager::latestVersion() const {
  return latest.version;
}

uint32_t OtaManager::latestBuild() const {
  return latest.buildNumber;
}

String OtaManager::latestChannel() const {
  return latest.channel;
}

String OtaManager::latestBuildInfo() const {
  if (latest.version.isEmpty()) {
    return "未检查";
  }
  String info = latest.channel + " " + latest.version + " build " + String(latest.buildNumber);
  if (!latest.gitCommit.isEmpty()) {
    info += " " + latest.gitCommit;
  }
  if (!latest.buildTime.isEmpty()) {
    info += " " + latest.buildTime;
  }
  return info;
}

String OtaManager::releaseNotes() const {
  return latest.releaseNotes;
}

String OtaManager::manifestUrl() const {
  return channelManifestUrl(OTA_CHANNEL);
}

String OtaManager::firmwareUrl() const {
  return resolveFirmwareUrl(latest);
}

String OtaManager::statusJson() const {
  String json;
  json.reserve(900);
  json += "{";
  json += "\"currentVersion\":\"" + jsonEscape(currentVersion()) + "\"";
  json += ",\"currentBuild\":" + String(currentBuild());
  json += ",\"currentChannel\":\"" + jsonEscape(currentChannel()) + "\"";
  json += ",\"latestVersion\":\"" + jsonEscape(latestVersion()) + "\"";
  json += ",\"latestBuild\":" + String(latestBuild());
  json += ",\"latestChannel\":\"" + jsonEscape(latestChannel()) + "\"";
  json += ",\"latestBuildInfo\":\"" + jsonEscape(latestBuildInfo()) + "\"";
  json += ",\"releaseNotes\":\"" + jsonEscape(releaseNotes()) + "\"";
  json += ",\"updateAvailable\":";
  json += updateAvailable() ? "true" : "false";
  json += ",\"busy\":";
  json += isBusy() ? "true" : "false";
  json += ",\"status\":\"" + jsonEscape(statusText()) + "\"";
  json += ",\"lastError\":\"" + jsonEscape(lastErrorText()) + "\"";
  json += ",\"manifestUrl\":\"" + jsonEscape(manifestUrl()) + "\"";
  json += ",\"firmwareUrl\":\"" + jsonEscape(firmwareUrl()) + "\"";
  json += ",\"devBootAttempts\":" + String(devBootAttempts);
  json += ",\"healthy\":";
  json += healthyMarked ? "true" : "false";
  json += "}";
  return json;
}

String OtaManager::channelManifestUrl(const String& channel) const {
  return normalizeBaseUrl() + "/" + channel + "/manifest.json";
}

bool OtaManager::fetchManifest(const String& channel, OtaManifest& out, String& error) {
  String url = channelManifestUrl(channel);
  WiFiClient plainClient;
  WiFiClientSecure client;
  WiFiClient* transport = &plainClient;
  if (url.startsWith("https://")) {
    client.setInsecure();
    transport = &client;
  }
  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  if (!http.begin(*transport, url)) {
    error = "无法打开 manifest URL: " + url;
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "manifest HTTP " + String(code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  return parseManifest(payload, out, error);
}

bool OtaManager::parseManifest(const String& payload, OtaManifest& out, String& error) const {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, payload);
  if (jsonError) {
    error = "manifest JSON 解析失败: ";
    error += jsonError.c_str();
    return false;
  }

  out.channel = doc["channel"] | "";
  out.version = doc["version"] | "";
  out.buildNumber = doc["build_number"] | doc["build"] | 0;
  out.gitBranch = doc["git_branch"] | "";
  out.gitCommit = doc["git_commit"] | "";
  out.buildTime = doc["build_time"] | "";
  out.firmwareUrl = doc["firmware_url"] | "";
  out.sha256 = doc["sha256"] | "";
  out.size = doc["size"] | 0;
  out.minSupportedVersion = doc["min_supported_version"] | "";
  out.releaseNotes = doc["release_notes"] | "";

  out.sha256.toLowerCase();
  if (out.channel.isEmpty() || out.version.isEmpty() || out.firmwareUrl.isEmpty() ||
      out.sha256.isEmpty() || out.size == 0) {
    error = "manifest 缺少必要字段";
    return false;
  }
  if (!isHexSha256(out.sha256)) {
    error = "manifest sha256 格式错误";
    return false;
  }
  if (!out.minSupportedVersion.isEmpty() &&
      compareVersions(currentVersion(), out.minSupportedVersion) < 0) {
    error = "当前版本低于 min_supported_version";
    return false;
  }
  return true;
}

bool OtaManager::isManifestNewer(const OtaManifest& manifest) const {
  if (manifest.buildNumber > currentBuild()) {
    return true;
  }
  if (manifest.buildNumber < currentBuild()) {
    return false;
  }
  return compareVersions(manifest.version, currentVersion()) > 0;
}

bool OtaManager::installManifest(const OtaManifest& manifest, String& error) {
  state = State::Upgrading;
  error = "";
  if (!downloadAndInstall(manifest, error)) {
    lastError = error;
    state = State::Error;
    return false;
  }
  return true;
}

bool OtaManager::downloadAndInstall(const OtaManifest& manifest, String& error) {
  String url = resolveFirmwareUrl(manifest);
  if (url.isEmpty()) {
    error = "firmware_url 为空";
    return false;
  }

  WiFiClientSecure client;
  WiFiClient plainClient;
  WiFiClient* transport = &plainClient;
  if (url.startsWith("https://")) {
    client.setInsecure();
    transport = &client;
  }
  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  if (!http.begin(*transport, url)) {
    error = "无法打开固件 URL: " + url;
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "固件下载 HTTP " + String(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    error = "固件大小无效";
    http.end();
    return false;
  }
  if (manifest.size > 0 && static_cast<size_t>(contentLength) != manifest.size) {
    error = "固件大小不匹配";
    http.end();
    return false;
  }
  if (!Update.begin(static_cast<size_t>(contentLength), U_FLASH)) {
    error = "Update.begin 失败: " + String(Update.errorString());
    http.end();
    return false;
  }

  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  unsigned long lastDataAt = millis();

  while (http.connected() && written < static_cast<size_t>(contentLength)) {
    size_t available = stream->available();
    if (available == 0) {
      if (millis() - lastDataAt > OTA_HTTP_TIMEOUT_MS) {
        error = "固件下载超时";
        Update.abort();
        mbedtls_sha256_free(&shaCtx);
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    size_t readLen = min(available, sizeof(buffer));
    int bytesRead = stream->readBytes(buffer, readLen);
    if (bytesRead <= 0) {
      continue;
    }
    lastDataAt = millis();
    mbedtls_sha256_update(&shaCtx, buffer, static_cast<size_t>(bytesRead));
    size_t bytesWritten = Update.write(buffer, static_cast<size_t>(bytesRead));
    if (bytesWritten != static_cast<size_t>(bytesRead)) {
      error = "固件写入失败: " + String(Update.errorString());
      Update.abort();
      mbedtls_sha256_free(&shaCtx);
      http.end();
      return false;
    }
    written += bytesWritten;
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&shaCtx, digest);
  mbedtls_sha256_free(&shaCtx);

  String actualSha;
  actualSha.reserve(64);
  for (uint8_t value : digest) {
    actualSha += hexByte(value);
  }
  actualSha.toLowerCase();
  if (actualSha != manifest.sha256) {
    error = "SHA256 校验失败";
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end(true)) {
    error = "Update.end 失败: " + String(Update.errorString());
    http.end();
    return false;
  }
  http.end();

  Preferences p;
  if (p.begin(OTA_PREF_NAMESPACE, false)) {
    p.putBool(OTA_PREF_HEALTHY, false);
    p.putUInt(OTA_PREF_BOOT_ATTEMPTS, 0);
    p.putString(OTA_PREF_VERSION, manifest.version);
    p.putUInt(OTA_PREF_BUILD, manifest.buildNumber);
    p.putString(OTA_PREF_CHANNEL, manifest.channel);
    p.end();
  }

  delay(300);
  ESP.restart();
  return true;
}

bool OtaManager::markHealthy() {
  healthyMarked = true;
  Preferences p;
  if (!p.begin(OTA_PREF_NAMESPACE, false)) {
    return false;
  }
  p.putBool(OTA_PREF_HEALTHY, true);
  p.putUInt(OTA_PREF_BOOT_ATTEMPTS, 0);
  p.putString(OTA_PREF_VERSION, currentVersion());
  p.putUInt(OTA_PREF_BUILD, currentBuild());
  p.putString(OTA_PREF_CHANNEL, currentChannel());
  p.end();
  devBootAttempts = 0;
  return true;
}

void OtaManager::noteBoot() {
  Preferences p;
  if (!p.begin(OTA_PREF_NAMESPACE, false)) {
    return;
  }
  bool wasHealthy = p.getBool(OTA_PREF_HEALTHY, false);
  String storedVersion = p.getString(OTA_PREF_VERSION, "");
  uint32_t storedBuild = p.getUInt(OTA_PREF_BUILD, 0);
  String storedChannel = p.getString(OTA_PREF_CHANNEL, "");
  bool sameFirmware = storedVersion == currentVersion() && storedBuild == currentBuild() &&
                      storedChannel == currentChannel();
  devBootAttempts = p.getUInt(OTA_PREF_BOOT_ATTEMPTS, 0);
  if (!sameFirmware || wasHealthy) {
    devBootAttempts = 0;
  }
  devBootAttempts++;
  p.putBool(OTA_PREF_HEALTHY, false);
  p.putUInt(OTA_PREF_BOOT_ATTEMPTS, devBootAttempts);
  p.putString(OTA_PREF_VERSION, currentVersion());
  p.putUInt(OTA_PREF_BUILD, currentBuild());
  p.putString(OTA_PREF_CHANNEL, currentChannel());
  p.end();
}

bool OtaManager::shouldAttemptDevFallback(bool networkConnected, bool webReady, bool oledReady) {
  if (fallbackAttempted || healthyMarked || currentChannel() != "dev") {
    return false;
  }
  if (devBootAttempts <= OTA_FALLBACK_MAX_DEV_BOOT_ATTEMPTS) {
    return false;
  }
  return networkConnected && webReady && oledReady;
}

String OtaManager::resolveFirmwareUrl(const OtaManifest& manifest) const {
  if (manifest.firmwareUrl.startsWith("http://") || manifest.firmwareUrl.startsWith("https://")) {
    return manifest.firmwareUrl;
  }
  if (manifest.firmwareUrl.startsWith("/")) {
    return normalizeBaseUrl() + manifest.firmwareUrl;
  }
  String channel = manifest.channel.isEmpty() ? String(OTA_CHANNEL) : manifest.channel;
  return normalizeBaseUrl() + "/" + channel + "/" + manifest.firmwareUrl;
}

String OtaManager::stateName() const {
  switch (state) {
    case State::Idle:
      return "idle";
    case State::Checking:
      return "checking";
    case State::UpdateAvailable:
      return "update_available";
    case State::UpToDate:
      return "up_to_date";
    case State::Upgrading:
      return "upgrading";
    case State::Error:
      return "error";
  }
  return "unknown";
}

String OtaManager::jsonEscape(String value) const {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "\\r");
  value.replace("\n", "\\n");
  return value;
}

String OtaManager::normalizeBaseUrl() const {
  String base = OTA_BASE_URL;
  while (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base;
}

bool OtaManager::isHexSha256(const String& value) {
  if (value.length() != 64) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) {
      return false;
    }
  }
  return true;
}

int OtaManager::compareVersions(const String& lhs, const String& rhs) {
  int leftStart = 0;
  int rightStart = 0;
  while (leftStart < static_cast<int>(lhs.length()) || rightStart < static_cast<int>(rhs.length())) {
    int leftDot = lhs.indexOf('.', leftStart);
    int rightDot = rhs.indexOf('.', rightStart);
    if (leftDot < 0) {
      leftDot = lhs.length();
    }
    if (rightDot < 0) {
      rightDot = rhs.length();
    }
    long leftPart = lhs.substring(leftStart, leftDot).toInt();
    long rightPart = rhs.substring(rightStart, rightDot).toInt();
    if (leftPart != rightPart) {
      return leftPart > rightPart ? 1 : -1;
    }
    leftStart = leftDot + 1;
    rightStart = rightDot + 1;
  }
  return 0;
}

#include "OtaManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "Config.h"

namespace {
const char* OTA_PREF_NAMESPACE = "amap_ota";
const char* OTA_PREF_HEALTHY = "healthy";
const char* OTA_PREF_BOOT_ATTEMPTS = "boot_attempts";
const char* OTA_PREF_VERSION = "version";
const char* OTA_PREF_BUILD = "build";
const char* OTA_PREF_CHANNEL = "channel";
const char* OTA_PREF_SELECTED_CHANNEL = "selected_channel";
const uint8_t OTA_HTTP_REDIRECT_LIMIT = 4;
const uint32_t OTA_PROGRESS_DOWNLOAD_MAX = 96;
const uint32_t OTA_TASK_STACK_SIZE = 10240;
const UBaseType_t OTA_TASK_PRIORITY = 1;

struct OtaTaskContext {
  OtaManager* manager;
  OtaManifest manifest;
};

String hexByte(uint8_t value) {
  const char* digits = "0123456789abcdef";
  String out;
  out += digits[value >> 4];
  out += digits[value & 0x0F];
  return out;
}
}  // namespace

void OtaManager::begin() {
  ensureStateMutex();
  bootStartedAt = millis();
  loadSelectedChannel();
  noteBoot();
  Serial.printf("OTA boot: channel=%s version=%s build=%lu selected=%s attempts=%lu healthy=%s\n",
                currentChannel().c_str(),
                currentVersion().c_str(),
                static_cast<unsigned long>(currentBuild()),
                selectedChannel().c_str(),
                static_cast<unsigned long>(devBootAttempts),
                healthyMarked ? "true" : "false");
}

void OtaManager::update(bool networkConnected, bool webReady, bool oledReady) {
  const unsigned long now = millis();
  if (!healthyMarked && networkConnected && webReady && oledReady &&
      now - bootStartedAt >= OTA_HEALTHY_MARK_DELAY_MS) {
    markHealthy();
  }

  if (shouldAttemptDevFallback(networkConnected, webReady, oledReady)) {
    fallbackAttempted = true;
    fallbackToStableNow();
    return;
  }

  if (networkConnected && !isBusy() &&
      (lastCheckAt == 0 || now - lastCheckAt >= OTA_CHECK_INTERVAL_MS)) {
    checkNow();
  }
}

bool OtaManager::checkNow() {
  {
    ensureStateMutex();
    if (isBusy()) {
      lockState();
      lastError = "OTA 正忙，请稍后再试。";
      unlockState();
      return false;
    }

    lockState();
    state = State::Checking;
    lastError = "";
    lastCheckAt = millis();
    clearLatestState();
    resetProgressLocked();
    progressStatus = "正在检查更新";
    unlockState();

    OtaManifest manifestCopy;
    String errorCopy;
    const String channel = selectedChannel();
    if (!fetchManifest(channel, manifestCopy, errorCopy)) {
      lockState();
      lastError = errorCopy;
      state = State::Error;
      progressStatus = "检查失败";
      unlockState();
      return false;
    }

    const bool newer = isManifestNewer(manifestCopy);
    lockState();
    latest = manifestCopy;
    state = newer ? State::UpdateAvailable : State::UpToDate;
    progressStatus = newer ? "发现可用更新" : "当前已经是最新版本";
    unlockState();
    return true;
  }
  if (isBusy()) {
    lastError = "OTA 正忙，请稍后再试。";
    return false;
  }

  state = State::Checking;
  lastError = "";
  lastCheckAt = millis();
  clearLatestState();

  OtaManifest manifest;
  String error;
  const String channel = selectedChannel();
  if (!fetchManifest(channel, manifest, error)) {
    lastError = error;
    state = State::Error;
    return false;
  }

  latest = manifest;
  state = isManifestNewer(latest) ? State::UpdateAvailable : State::UpToDate;
  return true;
}

bool OtaManager::upgradeNow() {
  {
    ensureStateMutex();
    if (isBusy()) {
      lockState();
      lastError = "OTA 正忙，请稍后再试。";
      unlockState();
      return false;
    }

    OtaManifest manifestCopy;
    lockState();
    manifestCopy = latest;
    unlockState();

    if (manifestCopy.firmwareUrl.isEmpty() || manifestCopy.channel != selectedChannel()) {
      if (!checkNow()) {
        return false;
      }
      lockState();
      manifestCopy = latest;
      unlockState();
    }
    if (!isManifestNewer(manifestCopy)) {
      lockState();
      state = State::UpToDate;
      lastError = "当前已经是最新版本。";
      progressStatus = "当前已经是最新版本";
      unlockState();
      return false;
    }

    String errorCopy;
    return installManifest(manifestCopy, errorCopy);
  }
  if (isBusy()) {
    lastError = "OTA 正忙，请稍后再试。";
    return false;
  }
  if (latest.firmwareUrl.isEmpty() || latest.channel != selectedChannel()) {
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
  {
    ensureStateMutex();
    if (isBusy()) {
      return false;
    }

    lockState();
    state = State::Checking;
    lastError = "";
    resetProgressLocked();
    progressStatus = "正在检查 stable 回退包";
    unlockState();

    OtaManifest stableManifest;
    String errorCopy;
    if (!fetchManifest("stable", stableManifest, errorCopy)) {
      lockState();
      lastError = "stable 回退检查失败：" + errorCopy;
      state = State::Error;
      progressStatus = "回退检查失败";
      unlockState();
      return false;
    }

    return installManifest(stableManifest, errorCopy);
  }
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
  {
    lockState();
    const State currentState = state;
    const OtaManifest manifestCopy = latest;
    unlockState();
    return currentState == State::UpdateAvailable && isManifestNewer(manifestCopy);
  }
  return state == State::UpdateAvailable && isManifestNewer(latest);
}

bool OtaManager::isBusy() const {
  {
    lockState();
    const bool busy = state == State::Checking || state == State::Upgrading;
    unlockState();
    return busy;
  }
  return state == State::Checking || state == State::Upgrading;
}

String OtaManager::statusText() const {
  return stateName();
}

String OtaManager::lastErrorText() const {
  {
    lockState();
    const String errorCopy = lastError;
    unlockState();
    return errorCopy;
  }
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

String OtaManager::selectedChannel() const {
  return selectedChannelName.isEmpty() ? defaultSelectedChannel() : selectedChannelName;
}

bool OtaManager::setSelectedChannel(const String& channel) {
  {
    ensureStateMutex();
    if (channel != "stable" && channel != "dev") {
      lockState();
      lastError = "不支持的 OTA 渠道：" + channel;
      unlockState();
      return false;
    }
    if (channel == selectedChannel()) {
      lockState();
      lastError = "";
      unlockState();
      return true;
    }

    Preferences p;
    if (!p.begin(OTA_PREF_NAMESPACE, false)) {
      lockState();
      lastError = "无法打开 OTA 配置存储。";
      unlockState();
      return false;
    }
    p.putString(OTA_PREF_SELECTED_CHANNEL, channel);
    p.end();

    selectedChannelName = channel;
    lockState();
    clearLatestState();
    lastError = "";
    state = State::Idle;
    lastCheckAt = 0;
    resetProgressLocked();
    unlockState();
    Serial.printf("OTA selected channel changed: %s\n", selectedChannelName.c_str());
    return true;
  }
  #if 0
  if (channel != "stable" && channel != "dev") {
    lastError = "不支持的 OTA 渠道：" + channel;
    return false;
  }

  lockState();
  updateProgressLocked("正在完成升级", 99, written, static_cast<size_t>(contentLength));
  unlockState();

  Preferences p;
  if (!p.begin(OTA_PREF_NAMESPACE, false)) {
    lastError = "无法打开 OTA 配置存储。";
    return false;
  }
  p.putString(OTA_PREF_SELECTED_CHANNEL, channel);
  p.end();

  selectedChannelName = channel;
  clearLatestState();
  lastError = "";
  state = State::Idle;
  lastCheckAt = 0;
  Serial.printf("OTA selected channel changed: %s\n", selectedChannelName.c_str());
  return true;
  #endif
}

String OtaManager::latestVersion() const {
  {
    lockState();
    const String value = latest.version;
    unlockState();
    return value;
  }
  return latest.version;
}

uint32_t OtaManager::latestBuild() const {
  {
    lockState();
    const uint32_t value = latest.buildNumber;
    unlockState();
    return value;
  }
  return latest.buildNumber;
}

String OtaManager::latestChannel() const {
  {
    lockState();
    const String value = latest.channel;
    unlockState();
    return value;
  }
  return latest.channel;
}

String OtaManager::latestBuildInfo() const {
  {
    lockState();
    const OtaManifest manifestCopy = latest;
    unlockState();
    if (manifestCopy.version.isEmpty()) {
      return "未检查";
    }
    String info = manifestCopy.channel + " " + manifestCopy.version + " build " +
                  String(manifestCopy.buildNumber);
    if (!manifestCopy.gitCommit.isEmpty()) {
      info += " " + manifestCopy.gitCommit;
    }
    if (!manifestCopy.buildTime.isEmpty()) {
      info += " " + manifestCopy.buildTime;
    }
    return info;
  }
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
  {
    lockState();
    const String value = latest.releaseNotes;
    unlockState();
    return value;
  }
  return latest.releaseNotes;
}

String OtaManager::manifestUrl() const {
  return channelManifestUrl(selectedChannel());
}

String OtaManager::firmwareUrl() const {
  {
    lockState();
    const OtaManifest manifestCopy = latest;
    unlockState();
    return resolveFirmwareUrl(manifestCopy);
  }
  return resolveFirmwareUrl(latest);
}

uint8_t OtaManager::progressPercent() const {
  lockState();
  const uint8_t value = progressPercentValue;
  unlockState();
  return value;
}

String OtaManager::progressText() const {
  lockState();
  const String status = progressStatus;
  const size_t currentBytes = progressCurrentBytes;
  const size_t totalBytes = progressTotalBytes;
  const uint8_t percent = progressPercentValue;
  const bool rebooting = rebootPending;
  unlockState();

  if (status.isEmpty()) {
    return "未开始";
  }
  if (totalBytes == 0 || rebooting) {
    return status;
  }
  const size_t currentKb = currentBytes / 1024;
  const size_t totalKb = totalBytes / 1024;
  return status + " (" + String(percent) + "%, " + String(currentKb) + "/" + String(totalKb) +
         " KB)";
}

String OtaManager::statusJson() const {
  {
    lockState();
    const OtaManifest manifestCopy = latest;
    const State currentState = state;
    const String errorCopy = lastError;
    const size_t currentBytes = progressCurrentBytes;
    const size_t totalBytes = progressTotalBytes;
    const uint8_t percent = progressPercentValue;
    const bool rebooting = rebootPending;
    unlockState();

    String json;
    json.reserve(1180);
    json += "{";
    json += "\"currentVersion\":\"" + jsonEscape(currentVersion()) + "\"";
    json += ",\"currentBuild\":" + String(currentBuild());
    json += ",\"currentChannel\":\"" + jsonEscape(currentChannel()) + "\"";
    json += ",\"selectedChannel\":\"" + jsonEscape(selectedChannel()) + "\"";
    json += ",\"latestVersion\":\"" + jsonEscape(manifestCopy.version) + "\"";
    json += ",\"latestBuild\":" + String(manifestCopy.buildNumber);
    json += ",\"latestChannel\":\"" + jsonEscape(manifestCopy.channel) + "\"";
    json += ",\"latestBuildInfo\":\"" + jsonEscape(latestBuildInfo()) + "\"";
    json += ",\"releaseNotes\":\"" + jsonEscape(manifestCopy.releaseNotes) + "\"";
    json += ",\"updateAvailable\":";
    json += (currentState == State::UpdateAvailable && isManifestNewer(manifestCopy)) ? "true"
                                                                                      : "false";
    json += ",\"busy\":";
    json += (currentState == State::Checking || currentState == State::Upgrading) ? "true"
                                                                                  : "false";
    json += ",\"status\":\"" + jsonEscape(stateName()) + "\"";
    json += ",\"lastError\":\"" + jsonEscape(errorCopy) + "\"";
    json += ",\"progressPercent\":" + String(percent);
    json += ",\"progressCurrentBytes\":" + String(static_cast<unsigned long>(currentBytes));
    json += ",\"progressTotalBytes\":" + String(static_cast<unsigned long>(totalBytes));
    json += ",\"progressText\":\"" + jsonEscape(progressText()) + "\"";
    json += ",\"rebootPending\":";
    json += rebooting ? "true" : "false";
    json += ",\"manifestUrl\":\"" + jsonEscape(manifestUrl()) + "\"";
    json += ",\"firmwareUrl\":\"" + jsonEscape(resolveFirmwareUrl(manifestCopy)) + "\"";
    json += ",\"devBootAttempts\":" + String(devBootAttempts);
    json += ",\"healthy\":";
    json += healthyMarked ? "true" : "false";
    json += "}";
    return json;
  }
  String json;
  json.reserve(960);
  json += "{";
  json += "\"currentVersion\":\"" + jsonEscape(currentVersion()) + "\"";
  json += ",\"currentBuild\":" + String(currentBuild());
  json += ",\"currentChannel\":\"" + jsonEscape(currentChannel()) + "\"";
  json += ",\"selectedChannel\":\"" + jsonEscape(selectedChannel()) + "\"";
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
  WiFiClientSecure secureClient;
  HTTPClient http;
  int code = 0;
  if (!openHttpGet(url, http, plainClient, secureClient, code, error)) {
    return false;
  }
  if (code != HTTP_CODE_OK) {
    error = "manifest HTTP " + String(code);
    http.end();
    return false;
  }
  const String payload = http.getString();
  http.end();
  return parseManifest(payload, out, error);
}

bool OtaManager::parseManifest(const String& payload, OtaManifest& out, String& error) const {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, payload);
  if (jsonError) {
    error = "manifest JSON 解析失败：";
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
  {
    ensureStateMutex();
    error = "";
    return startInstallTask(manifest, error);
  }
  state = State::Upgrading;
  error = "";
  Serial.printf("OTA install start: target_channel=%s target_version=%s target_build=%lu\n",
                manifest.channel.c_str(),
                manifest.version.c_str(),
                static_cast<unsigned long>(manifest.buildNumber));
  if (!downloadAndInstall(manifest, error)) {
    lastError = error;
    state = State::Error;
    Serial.printf("OTA install failed: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool OtaManager::downloadAndInstall(const OtaManifest& manifest, String& error) {
  String url = resolveFirmwareUrl(manifest);
  lockState();
  updateProgressLocked("正在连接固件服务器", 1, 0, manifest.size);
  unlockState();
  if (url.isEmpty()) {
    error = "firmware_url 为空";
    return false;
  }

  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  HTTPClient http;
  int code = 0;
  if (!openHttpGet(url, http, plainClient, secureClient, code, error)) {
    return false;
  }
  if (code != HTTP_CODE_OK) {
    error = "固件下载 HTTP " + String(code);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
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
    error = "Update.begin 失败：" + String(Update.errorString());
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
  unsigned long lastProgressAt = 0;

  lockState();
  updateProgressLocked("正在下载并写入固件", 1, 0, static_cast<size_t>(contentLength));
  unlockState();

  while (http.connected() && written < static_cast<size_t>(contentLength)) {
    const size_t available = stream->available();
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

    const size_t readLen = min(available, sizeof(buffer));
    const int bytesRead = stream->readBytes(buffer, readLen);
    if (bytesRead <= 0) {
      continue;
    }
    lastDataAt = millis();
    mbedtls_sha256_update(&shaCtx, buffer, static_cast<size_t>(bytesRead));
    const size_t bytesWritten = Update.write(buffer, static_cast<size_t>(bytesRead));
    if (bytesWritten != static_cast<size_t>(bytesRead)) {
      error = "固件写入失败：" + String(Update.errorString());
      Update.abort();
      mbedtls_sha256_free(&shaCtx);
      http.end();
      return false;
    }
    written += bytesWritten;
    const unsigned long now = millis();
    if (contentLength > 0 &&
        (now - lastProgressAt >= 200 || written == static_cast<size_t>(contentLength))) {
      uint32_t percentValue =
          (static_cast<uint32_t>(written) * OTA_PROGRESS_DOWNLOAD_MAX) /
          static_cast<uint32_t>(contentLength);
      if (percentValue > OTA_PROGRESS_DOWNLOAD_MAX) {
        percentValue = OTA_PROGRESS_DOWNLOAD_MAX;
      }
      const uint8_t percent = static_cast<uint8_t>(percentValue);
      lockState();
      updateProgressLocked("正在下载并写入固件",
                           percent,
                           written,
                           static_cast<size_t>(contentLength));
      unlockState();
      lastProgressAt = now;
    }
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&shaCtx, digest);
  mbedtls_sha256_free(&shaCtx);

  lockState();
  updateProgressLocked("正在校验固件", 97, written, static_cast<size_t>(contentLength));
  unlockState();

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
    error = "Update.end 失败：" + String(Update.errorString());
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

  lockState();
  updateProgressLocked("正在完成升级", 99, written, static_cast<size_t>(contentLength));
  unlockState();
  Serial.printf("OTA install complete: channel=%s version=%s build=%lu, rebooting\n",
                manifest.channel.c_str(),
                manifest.version.c_str(),
                static_cast<unsigned long>(manifest.buildNumber));
  lockState();
  updateProgressLocked("升级完成，正在重启", 100, written, static_cast<size_t>(contentLength), true);
  unlockState();
  delay(300);
  ESP.restart();
  return true;
}

bool OtaManager::openHttpGet(String& url,
                             HTTPClient& http,
                             WiFiClient& plainClient,
                             WiFiClientSecure& secureClient,
                             int& code,
                             String& error) {
  for (uint8_t attempt = 0; attempt <= OTA_HTTP_REDIRECT_LIMIT; ++attempt) {
    http.end();
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);

    bool started = false;
    if (url.startsWith("https://")) {
      secureClient.setInsecure();
      started = http.begin(secureClient, url);
    } else {
      started = http.begin(plainClient, url);
    }

    if (!started) {
      error = "无法打开 URL：" + url;
      return false;
    }

    code = http.GET();
    if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND ||
        code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT ||
        code == HTTP_CODE_PERMANENT_REDIRECT) {
      const String location = http.getLocation();
      if (location.isEmpty()) {
        error = "重定向缺少 Location：" + url;
        return false;
      }
      Serial.printf("HTTP redirect: %s -> %s (%d)\n", url.c_str(), location.c_str(), code);
      url = location;
      continue;
    }
    return true;
  }

  error = "重定向次数过多：" + url;
  return false;
}

void OtaManager::loadSelectedChannel() {
  selectedChannelName = defaultSelectedChannel();
  Preferences p;
  if (!p.begin(OTA_PREF_NAMESPACE, true)) {
    return;
  }
  String stored = selectedChannelName;
  if (p.isKey(OTA_PREF_SELECTED_CHANNEL)) {
    stored = p.getString(OTA_PREF_SELECTED_CHANNEL, selectedChannelName);
  }
  p.end();
  if (stored == "stable" || stored == "dev") {
    selectedChannelName = stored;
  }
}

void OtaManager::clearLatestState() {
  latest = OtaManifest{};
}

String OtaManager::defaultSelectedChannel() const {
  return String(OTA_CHANNEL);
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
  Serial.printf("OTA marked healthy: channel=%s version=%s build=%lu\n",
                currentChannel().c_str(),
                currentVersion().c_str(),
                static_cast<unsigned long>(currentBuild()));
  return true;
}

void OtaManager::noteBoot() {
  Preferences p;
  if (!p.begin(OTA_PREF_NAMESPACE, false)) {
    return;
  }
  const bool wasHealthy = p.getBool(OTA_PREF_HEALTHY, false);
  const String storedVersion = p.getString(OTA_PREF_VERSION, "");
  const uint32_t storedBuild = p.getUInt(OTA_PREF_BUILD, 0);
  const String storedChannel = p.getString(OTA_PREF_CHANNEL, "");
  const bool sameFirmware = storedVersion == currentVersion() && storedBuild == currentBuild() &&
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
  healthyMarked = false;
  Serial.printf("OTA note boot: stored=%s/%lu/%s wasHealthy=%s sameFirmware=%s attempts=%lu\n",
                storedVersion.c_str(),
                static_cast<unsigned long>(storedBuild),
                storedChannel.c_str(),
                wasHealthy ? "true" : "false",
                sameFirmware ? "true" : "false",
                static_cast<unsigned long>(devBootAttempts));
}

bool OtaManager::shouldAttemptDevFallback(bool networkConnected, bool webReady, bool oledReady) {
  if (fallbackAttempted || healthyMarked || currentChannel() != "dev") {
    return false;
  }
  if (devBootAttempts <= OTA_FALLBACK_MAX_DEV_BOOT_ATTEMPTS) {
    return false;
  }
  if (millis() - bootStartedAt < OTA_HEALTHY_MARK_DELAY_MS) {
    return false;
  }
  if (networkConnected && webReady && oledReady) {
    Serial.printf("OTA fallback triggered: attempts=%lu channel=%s\n",
                  static_cast<unsigned long>(devBootAttempts),
                  currentChannel().c_str());
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
  const String channel = manifest.channel.isEmpty() ? selectedChannel() : manifest.channel;
  return normalizeBaseUrl() + "/" + channel + "/" + manifest.firmwareUrl;
}

String OtaManager::stateName() const {
  {
    lockState();
    const State currentState = state;
    unlockState();
    switch (currentState) {
      case State::Idle:
        return "空闲";
      case State::Checking:
        return "检查中";
      case State::UpdateAvailable:
        return "发现更新";
      case State::UpToDate:
        return "已是最新";
      case State::Upgrading:
        return "升级中";
      case State::Error:
        return "错误";
    }
    return "未知";
  }
  switch (state) {
    case State::Idle:
      return "空闲";
    case State::Checking:
      return "检查中";
    case State::UpdateAvailable:
      return "发现更新";
    case State::UpToDate:
      return "已是最新";
    case State::Upgrading:
      return "升级中";
    case State::Error:
      return "错误";
  }
  return "未知";
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
    const char c = value[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
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
    const long leftPart = lhs.substring(leftStart, leftDot).toInt();
    const long rightPart = rhs.substring(rightStart, rightDot).toInt();
    if (leftPart != rightPart) {
      return leftPart > rightPart ? 1 : -1;
    }
    leftStart = leftDot + 1;
    rightStart = rightDot + 1;
  }
  return 0;
}

void OtaManager::ensureStateMutex() {
  if (stateMutex == nullptr) {
    stateMutex = xSemaphoreCreateMutex();
  }
}

void OtaManager::lockState() const {
  if (stateMutex != nullptr) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
  }
}

void OtaManager::unlockState() const {
  if (stateMutex != nullptr) {
    xSemaphoreGive(stateMutex);
  }
}

void OtaManager::resetProgressLocked() {
  progressStatus = "";
  progressCurrentBytes = 0;
  progressTotalBytes = 0;
  progressPercentValue = 0;
  rebootPending = false;
}

void OtaManager::updateProgressLocked(const String& status,
                                      uint8_t percent,
                                      size_t currentBytes,
                                      size_t totalBytes,
                                      bool rebooting) {
  progressStatus = status;
  progressPercentValue = percent;
  progressCurrentBytes = currentBytes;
  progressTotalBytes = totalBytes;
  rebootPending = rebooting;
}

bool OtaManager::startInstallTask(const OtaManifest& manifest, String& error) {
  ensureStateMutex();
  lockState();
  if (upgradeTaskRunning || state == State::Upgrading) {
    lastError = "OTA 正忙，请稍后再试。";
    error = lastError;
    unlockState();
    return false;
  }

  state = State::Upgrading;
  lastError = "";
  resetProgressLocked();
  updateProgressLocked("准备开始 OTA 升级", 0, 0, manifest.size);
  latest = manifest;
  upgradeTaskRunning = true;
  unlockState();

  OtaTaskContext* context = new OtaTaskContext{this, manifest};
  if (context == nullptr) {
    lockState();
    upgradeTaskRunning = false;
    state = State::Error;
    lastError = "内存不足，无法启动 OTA 任务。";
    error = lastError;
    unlockState();
    return false;
  }

  const BaseType_t taskCreated = xTaskCreate(
      OtaManager::upgradeTaskEntry, "ota_upgrade", OTA_TASK_STACK_SIZE, context, OTA_TASK_PRIORITY, nullptr);
  if (taskCreated != pdPASS) {
    delete context;
    lockState();
    upgradeTaskRunning = false;
    state = State::Error;
    lastError = "无法启动 OTA 后台任务。";
    error = lastError;
    unlockState();
    return false;
  }

  Serial.printf("OTA install queued: target_channel=%s target_version=%s target_build=%lu\n",
                manifest.channel.c_str(),
                manifest.version.c_str(),
                static_cast<unsigned long>(manifest.buildNumber));
  return true;
}

void OtaManager::upgradeTaskEntry(void* parameter) {
  OtaTaskContext* context = static_cast<OtaTaskContext*>(parameter);
  if (context != nullptr && context->manager != nullptr) {
    context->manager->runUpgradeTask(context->manifest);
  }
  delete context;
  vTaskDelete(nullptr);
}

void OtaManager::runUpgradeTask(OtaManifest manifest) {
  String error;
  Serial.printf("OTA install start: target_channel=%s target_version=%s target_build=%lu\n",
                manifest.channel.c_str(),
                manifest.version.c_str(),
                static_cast<unsigned long>(manifest.buildNumber));

  if (!downloadAndInstall(manifest, error)) {
    lockState();
    lastError = error;
    state = State::Error;
    progressStatus = "升级失败";
    rebootPending = false;
    upgradeTaskRunning = false;
    unlockState();
    Serial.printf("OTA install failed: %s\n", error.c_str());
    return;
  }

  lockState();
  upgradeTaskRunning = false;
  unlockState();
}

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>

class HTTPClient;
class WiFiClient;
class WiFiClientSecure;

struct OtaManifest {
  String channel;
  String version;
  uint32_t buildNumber = 0;
  String gitBranch;
  String gitCommit;
  String buildTime;
  String firmwareUrl;
  String sha256;
  size_t size = 0;
  String minSupportedVersion;
  String releaseNotes;
  String changelogUrl;
};

class OtaManager {
public:
  void begin();
  void update(bool networkConnected, bool webReady, bool oledReady);

  bool checkNow();
  bool upgradeNow();
  bool fallbackToStableNow();

  bool updateAvailable() const;
  bool isBusy() const;
  String statusText() const;
  String lastErrorText() const;
  String currentVersion() const;
  uint32_t currentBuild() const;
  String currentChannel() const;
  String selectedChannel() const;
  bool setSelectedChannel(const String& channel);
  String latestVersion() const;
  uint32_t latestBuild() const;
  String latestChannel() const;
  String latestBuildInfo() const;
  String releaseNotes() const;
  String changelogUrl() const;
  String manifestUrl() const;
  String firmwareUrl() const;
  uint8_t progressPercent() const;
  String progressText() const;
  String statusJson() const;

private:
  enum class State {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Upgrading,
    Error,
  };

  Preferences prefs;
  OtaManifest latest;
  State state = State::Idle;
  String lastError;
  String progressStatus;
  unsigned long lastCheckAt = 0;
  unsigned long bootStartedAt = 0;
  bool healthyMarked = false;
  bool fallbackAttempted = false;
  uint32_t devBootAttempts = 0;
  String selectedChannelName;
  size_t progressCurrentBytes = 0;
  size_t progressTotalBytes = 0;
  uint8_t progressPercentValue = 0;
  bool rebootPending = false;
  bool upgradeTaskRunning = false;
  mutable SemaphoreHandle_t stateMutex = nullptr;

  String channelManifestUrl(const String& channel) const;
  bool fetchManifest(const String& channel, OtaManifest& out, String& error);
  bool fetchChangelog(const OtaManifest& manifest, String& out);
  bool parseManifest(const String& payload, OtaManifest& out, String& error) const;
  bool isManifestNewer(const OtaManifest& manifest) const;
  bool shouldAllowStableDowngrade(const OtaManifest& manifest) const;
  bool installManifest(const OtaManifest& manifest, String& error);
  bool downloadAndInstall(const OtaManifest& manifest, String& error);
  bool startInstallTask(const OtaManifest& manifest, String& error);
  bool openHttpGet(String& url,
                   HTTPClient& http,
                   WiFiClient& plainClient,
                   WiFiClientSecure& secureClient,
                   int& code,
                   String& error);
  void ensureStateMutex();
  void lockState() const;
  void unlockState() const;
  void resetProgressLocked();
  void updateProgressLocked(const String& status,
                            uint8_t percent,
                            size_t currentBytes,
                            size_t totalBytes,
                            bool rebooting = false);
  static void upgradeTaskEntry(void* parameter);
  void runUpgradeTask(OtaManifest manifest);
  void loadSelectedChannel();
  void clearLatestState();
  String defaultSelectedChannel() const;
  bool markHealthy();
  void noteBoot();
  bool shouldAttemptDevFallback(bool networkConnected, bool webReady, bool oledReady);
  String resolveFirmwareUrl(const OtaManifest& manifest) const;
  String resolveChangelogUrl(const OtaManifest& manifest) const;
  String stateName() const;
  String jsonEscape(String value) const;
  String normalizeBaseUrl() const;
  static bool isHexSha256(const String& value);
  static int compareVersions(const String& lhs, const String& rhs);
};

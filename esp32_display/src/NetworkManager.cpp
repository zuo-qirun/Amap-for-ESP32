#include "NetworkManager.h"

#include "Config.h"

#include <Esp.h>
#include <Update.h>

namespace {
const byte DNS_PORT = 53;
const char* WIFI_PREF_NAMESPACE = "amap_wifi";
const char* WIFI_PREF_SSID = "ssid";
const char* WIFI_PREF_PASSWORD = "password";
const char* DEV_PREF_NAMESPACE = "amap_dev";
const char* DEV_PREF_TFT_PREVIEW = "tft_preview";

bool isPlaceholderSsid(const String& ssid) {
  return ssid.isEmpty() || ssid == "car_hotspot_ssid";
}
}  // namespace

NetworkManager::NetworkManager()
    : webServer(80),
      portalIp(192, 168, 4, 1),
      portalGateway(192, 168, 4, 1),
      portalSubnet(255, 255, 255, 0) {}

void NetworkManager::begin(OtaManager* ota, const NavState* navigation, BleReceiver* ble) {
  otaManager = ota;
  navigationState = navigation;
  bleReceiver = ble;
  WiFi.persistent(false);
  // ESP32-S3 radio coexistence requires Wi-Fi modem sleep while BLE is
  // enabled. Disabling it makes the Wi-Fi task abort as soon as both radios
  // start, so keep power save enabled and let the coexistence scheduler work.
  WiFi.setSleep(true);
  portalSsidName = makePortalSsid();
  hardwareSettings = HardwareSettings::load();
  loadCredentials();
  loadDeveloperOptions();

  if (activeSsid.isEmpty()) {
    startConfigPortal("未配置 Wi-Fi，请连接配网热点。");
    return;
  }

  startStaConnect(true);
}

void NetworkManager::update() {
  if (webServerStarted) {
    webServer.handleClient();
  }
  if (portalActive) {
    dnsServer.processNextRequest();
  }

  const unsigned long now = millis();
  if (manualRebootAt != 0 && static_cast<long>(now - manualRebootAt) >= 0) {
    Serial.println("Manual firmware upload complete; rebooting");
    delay(50);
    ESP.restart();
    return;
  }
  if (hardwareRebootAt != 0 && static_cast<long>(now - hardwareRebootAt) >= 0) {
    Serial.println("Hardware settings saved; rebooting");
    delay(50);
    ESP.restart();
    return;
  }
  if (reconnectScheduled && static_cast<long>(now - reconnectAt) >= 0) {
    reconnectScheduled = false;
    if (activeSsid.isEmpty()) {
      staConnecting = false;
      startConfigPortal("未配置 Wi-Fi，请连接配网热点。");
    } else {
      startStaConnect(true);
    }
  }

  if (isConnected()) {
    staConnecting = false;
    lastError = "";
    if (portalActive) {
      stopConfigPortal();
    }
    beginUdpIfNeeded();
    return;
  }

  udpStarted = false;

  if (portalActive && !isPortalRadioActive()) {
    portalActive = false;
    lastError = "配网热点意外停止。";
    startConfigPortal("配网热点已重新启动。");
    return;
  }

  if (staConnecting && now - connectStartedAt >= AMAP_WIFI_CONNECT_TIMEOUT_MS) {
    staConnecting = false;
    lastError = "Wi-Fi 连接超时：" + wifiStatusName();
    startConfigPortal("Wi-Fi 不可用，设备已进入 AP 配网模式。");
    return;
  }

  if (activeSsid.isEmpty()) {
    startConfigPortal("未配置 Wi-Fi，请连接配网热点。");
    return;
  }

  if (!portalActive && !staConnecting && now - lastReconnectAttempt >= AMAP_WIFI_RETRY_MS) {
    startStaConnect(true);
  }
}

int NetworkManager::readPacket(char* buffer, size_t capacity, IPAddress& remoteIp, uint16_t& remotePort) {
  if (!udpStarted || capacity == 0) {
    return 0;
  }

  const int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return 0;
  }

  const size_t maxLen = capacity - 1;
  const int length = udp.read(buffer, min(static_cast<int>(maxLen), packetSize));
  if (length < 0) {
    return 0;
  }

  buffer[length] = '\0';
  remoteIp = udp.remoteIP();
  remotePort = udp.remotePort();
  return length;
}

bool NetworkManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool NetworkManager::isConfigPortalActive() const {
  return portalActive && isPortalRadioActive();
}

bool NetworkManager::isWebReady() const {
  return webServerStarted;
}

bool NetworkManager::isManualFirmwareUpdatePending() const {
  return manualUploadActive || manualRebootAt != 0;
}

String NetworkManager::ipString() const {
  if (isConnected()) {
    return WiFi.localIP().toString();
  }
  if (isConfigPortalActive()) {
    return portalIp.toString();
  }
  return "0.0.0.0";
}

String NetworkManager::configPortalSsid() const {
  return portalSsidName;
}

String NetworkManager::configPortalUrl() const {
  return "http://" + portalIp.toString() + "/";
}

String NetworkManager::statusText() const {
  if (isConnected()) {
    return "Wi-Fi " + activeSsid + " " + WiFi.localIP().toString();
  }
  if (isConfigPortalActive()) {
    return "配网热点 " + portalSsidName + " " + portalIp.toString();
  }
  if (staConnecting) {
    return "Wi-Fi 连接中 " + activeSsid;
  }
  return "Wi-Fi 未连接 " + wifiStatusName();
}

void NetworkManager::loadCredentials() {
  activeSsid = "";
  activePassword = "";
  credentialSource = "none";

  Preferences prefs;
  if (prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    activeSsid = prefs.getString(WIFI_PREF_SSID, "");
    activePassword = prefs.getString(WIFI_PREF_PASSWORD, "");
    prefs.end();
    if (!activeSsid.isEmpty()) {
      credentialSource = "saved";
      return;
    }
  }

  if (hasFallbackCredentials()) {
    activeSsid = AMAP_WIFI_SSID;
    activePassword = AMAP_WIFI_PASSWORD;
    credentialSource = "Config.h";
  }
}

void NetworkManager::saveCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  if (prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    prefs.putString(WIFI_PREF_SSID, ssid);
    prefs.putString(WIFI_PREF_PASSWORD, password);
    prefs.end();
  }

  activeSsid = ssid;
  activePassword = password;
  credentialSource = "saved";
}

void NetworkManager::clearSavedCredentials() {
  Preferences prefs;
  if (prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    prefs.remove(WIFI_PREF_SSID);
    prefs.remove(WIFI_PREF_PASSWORD);
    prefs.end();
  }

  activeSsid = "";
  activePassword = "";
  credentialSource = "none";
  loadCredentials();
}

void NetworkManager::loadDeveloperOptions() {
  Preferences prefs;
  if (!prefs.begin(DEV_PREF_NAMESPACE, true)) {
    developerPreviewEnabled = false;
    return;
  }
  developerPreviewEnabled = prefs.getBool(DEV_PREF_TFT_PREVIEW, false);
  prefs.end();
}

void NetworkManager::saveDeveloperPreview(bool enabled) {
  Preferences prefs;
  if (prefs.begin(DEV_PREF_NAMESPACE, false)) {
    prefs.putBool(DEV_PREF_TFT_PREVIEW, enabled);
    prefs.end();
  }
  developerPreviewEnabled = enabled;
}

bool NetworkManager::hasFallbackCredentials() const {
  const String fallbackSsid = AMAP_WIFI_SSID;
  return !isPlaceholderSsid(fallbackSsid);
}

void NetworkManager::scheduleStaConnect(unsigned long delayMs) {
  reconnectScheduled = true;
  reconnectAt = millis() + delayMs;
  lastReconnectAttempt = millis();
}

void NetworkManager::startStaConnect(bool force) {
  if (activeSsid.isEmpty()) {
    startConfigPortal("未配置 Wi-Fi，请连接配网热点。");
    return;
  }

  const unsigned long now = millis();
  if (!force && staConnecting && now - connectStartedAt < AMAP_WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }

  WiFi.mode(portalActive ? WIFI_AP_STA : WIFI_STA);
  startWebServerIfNeeded();
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(activeSsid.c_str(), activePassword.c_str());
  staConnecting = true;
  connectStartedAt = now;
  lastReconnectAttempt = now;
  portalMessage = "正在连接 Wi-Fi：" + activeSsid;
  Serial.printf("Wi-Fi connect start: ssid=%s portal=%s mode=%d\n",
                activeSsid.c_str(),
                portalActive ? "on" : "off",
                static_cast<int>(WiFi.getMode()));
}

void NetworkManager::startConfigPortal(const String& reason) {
  portalMessage = reason;
  staConnecting = false;

  if (portalActive && isPortalRadioActive()) {
    startWebServerIfNeeded();
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  delay(50);
  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.softAPConfig(portalIp, portalGateway, portalSubnet);

  const String apPassword = AMAP_CONFIG_AP_PASSWORD;
  bool apStarted = false;
  if (apPassword.length() >= 8) {
    apStarted = WiFi.softAP(portalSsidName.c_str(),
                            apPassword.c_str(),
                            AMAP_CONFIG_AP_CHANNEL,
                            false,
                            AMAP_CONFIG_AP_MAX_CLIENTS);
  } else {
    apStarted = WiFi.softAP(portalSsidName.c_str(),
                            nullptr,
                            AMAP_CONFIG_AP_CHANNEL,
                            false,
                            AMAP_CONFIG_AP_MAX_CLIENTS);
  }

  if (apStarted) {
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", portalIp);
    startWebServerIfNeeded();
    portalActive = true;
    lastError = "";
    Serial.printf("Config portal started: ssid=%s ip=%s mode=%d channel=%d\n",
                  portalSsidName.c_str(),
                  WiFi.softAPIP().toString().c_str(),
                  static_cast<int>(WiFi.getMode()),
                  AMAP_CONFIG_AP_CHANNEL);
  } else {
    portalActive = false;
    lastError = "配网热点启动失败。";
    Serial.println("Config portal failed to start");
  }
}

void NetworkManager::stopConfigPortal() {
  if (!portalActive) {
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
  portalMessage = "";
  Serial.println("Config portal stopped");
}

void NetworkManager::startWebServerIfNeeded() {
  configureRoutes();
  if (webServerStarted) {
    return;
  }
  webServer.begin();
  webServerStarted = true;
}

void NetworkManager::configureRoutes() {
  if (routesConfigured) {
    return;
  }

  webServer.on("/", HTTP_GET, [this]() { handleRoot(); });
  webServer.on("/save", HTTP_POST, [this]() { handleSave(); });
  webServer.on("/clear", HTTP_POST, [this]() { handleClear(); });
  webServer.on("/ota/check", HTTP_POST, [this]() { handleOtaCheck(); });
  webServer.on("/ota/upgrade", HTTP_POST, [this]() { handleOtaUpgrade(); });
  webServer.on("/ota/upgrade", HTTP_GET, [this]() { redirectToRoot(); });
  webServer.on("/developer/preview", HTTP_POST, [this]() { handleDeveloperPreview(); });
  webServer.on("/hardware/display", HTTP_POST, [this]() { handleHardwareSettings(); });
  webServer.on("/hardware/display", HTTP_GET, [this]() { redirectToRoot(); });
  webServer.on("/ble/clear", HTTP_POST, [this]() { handleBleClear(); });
  webServer.on("/firmware/upload", HTTP_POST,
               [this]() { handleManualFirmwareUploadComplete(); },
               [this]() { handleManualFirmwareUpload(); });
  webServer.on("/firmware/upload", HTTP_GET, [this]() { redirectToRoot(); });
  webServer.on("/status.json", HTTP_GET, [this]() { handleStatusJson(); });
  webServer.on("/tft.bmp", HTTP_GET, [this]() { handleTftBitmap(); });
  webServer.on("/generate_204", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/fwlink", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/hotspot-detect.html", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/canonical.html", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/ncsi.txt", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/connecttest.txt", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.onNotFound([this]() { handleNotFound(); });
  routesConfigured = true;
}

void NetworkManager::handleRoot() {
  if (shouldRedirectToPortal()) {
    redirectToPortal();
    return;
  }

  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", buildStatusPage());
}

void NetworkManager::handleSave() {
  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  ssid.trim();
  if (ssid.isEmpty()) {
    webServer.send(400, "text/html; charset=utf-8", buildStatusPage("Wi-Fi 名称不能为空。"));
    return;
  }

  saveCredentials(ssid, password);
  lastError = "";
  portalMessage = "已保存 Wi-Fi，正在尝试连接...";
  if (!isConnected()) {
    startConfigPortal(portalMessage);
  }
  scheduleStaConnect(800UL);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8",
                 buildStatusPage("已保存 Wi-Fi，ESP32 正在连接 " + ssid + "。"));
}

void NetworkManager::handleClear() {
  clearSavedCredentials();
  staConnecting = false;
  lastError = "";
  portalMessage = "已清除保存的 Wi-Fi，保持在配网模式。";
  if (!isConnected()) {
    startConfigPortal(portalMessage);
  }
  scheduleStaConnect(800UL);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8",
                 buildStatusPage("已清除保存的 Wi-Fi。"));
}

void NetworkManager::handleOtaCheck() {
  String message;
  if (isManualFirmwareUpdatePending()) {
    message = "手动固件上传正在进行或等待重启，请稍后再检查更新。";
  } else if (!otaManager) {
    message = "OTA 管理器尚未初始化。";
  } else if (!isConnected()) {
    message = "请先连接 Wi-Fi 再检查更新。";
  } else if (!applyOtaChannelSelection(message)) {
    message = "检查更新失败：" + message;
  } else if (otaManager->checkNow()) {
    message = otaManager->updateAvailable() ? "发现可用更新。" : "当前已经是最新版本。";
  } else {
    message = "检查更新失败：" + otaManager->lastErrorText();
  }

  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", buildStatusPage(message));
}

void NetworkManager::handleOtaUpgrade() {
  {
    String redirectMessage;
    if (isManualFirmwareUpdatePending()) {
      redirectMessage = "手动固件上传正在进行或等待重启，请稍后再升级。";
    } else if (!otaManager) {
      redirectMessage = "OTA 管理器尚未初始化。";
    } else if (!isConnected()) {
      redirectMessage = "请先连接 Wi-Fi 再升级。";
    } else if (!applyOtaChannelSelection(redirectMessage)) {
      redirectMessage = "升级失败：" + redirectMessage;
    } else if (otaManager->isBusy()) {
      portalMessage = "OTA 正在进行中，可在下方查看进度。";
      redirectToRoot();
      return;
    } else if (!otaManager->updateAvailable()) {
      redirectMessage = "当前没有可安装的更新，请先检查更新。";
    } else if (otaManager->upgradeNow()) {
      portalMessage = "开始 OTA 升级，可在下方查看进度；完成后设备会自动重启。";
      redirectToRoot();
      return;
    } else {
      redirectMessage = "升级失败：" + otaManager->lastErrorText();
    }

    webServer.sendHeader("Cache-Control", "no-store");
    webServer.send(200, "text/html; charset=utf-8", buildStatusPage(redirectMessage));
    return;
  }
  String message;
  if (isManualFirmwareUpdatePending()) {
    message = "手动固件上传正在进行或等待重启，请稍后再升级。";
  } else if (!otaManager) {
    message = "OTA 管理器尚未初始化。";
  } else if (!isConnected()) {
    message = "请先连接 Wi-Fi 再升级。";
  } else if (!applyOtaChannelSelection(message)) {
    message = "升级失败：" + message;
  } else if (!otaManager->updateAvailable()) {
    message = "当前没有可安装的更新，请先检查更新。";
  } else {
    message = "开始 OTA 升级，设备将自动重启。";
    webServer.sendHeader("Cache-Control", "no-store");
    webServer.send(200, "text/html; charset=utf-8", buildStatusPage(message));
    delay(200);
    otaManager->upgradeNow();
    return;
  }

  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", buildStatusPage(message));
}

void NetworkManager::handleDeveloperPreview() {
  saveDeveloperPreview(webServer.hasArg("enabled") && webServer.arg("enabled") == "1");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8",
                 buildStatusPage(developerPreviewEnabled ? "已开启 TFT 模拟显示。" : "已关闭 TFT 模拟显示。"));
}

void NetworkManager::handleHardwareSettings() {
  HardwareSettings requested = hardwareSettings;
  const String driver = webServer.arg("tftDriver");
  if (driver == "st7789") {
    requested.tftDriver = AMAP_TFT_DRIVER_ST7789;
  } else if (driver == "ili9341") {
    requested.tftDriver = AMAP_TFT_DRIVER_ILI9341;
  } else {
    webServer.send(400, "text/html; charset=utf-8",
                   buildStatusPage("不支持的屏幕芯片选项。"));
    return;
  }
  requested.touchEnabled = webServer.hasArg("touchEnabled") &&
                           webServer.arg("touchEnabled") == "1";
  requested.invertColors = webServer.hasArg("invertColors") &&
                           webServer.arg("invertColors") == "1";
  const bool changed = requested.tftDriver != hardwareSettings.tftDriver ||
                       requested.touchEnabled != hardwareSettings.touchEnabled ||
                       requested.invertColors != hardwareSettings.invertColors;
  if (!changed) {
    webServer.send(200, "text/html; charset=utf-8",
                   buildStatusPage("显示硬件设置没有变化。"));
    return;
  }
  if (!requested.save()) {
    webServer.send(500, "text/html; charset=utf-8",
                   buildStatusPage("无法保存显示硬件设置到 NVS。"));
    return;
  }
  hardwareSettings = requested;
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8",
                 buildStatusPage("显示硬件设置已保存，设备将在 2 秒内重启生效。"));
  hardwareRebootAt = millis() + 1800UL;
}

void NetworkManager::handleBleClear() {
  if (bleReceiver == nullptr) {
    webServer.send(503, "text/html; charset=utf-8",
                   buildStatusPage("BLE 尚未初始化。"));
    return;
  }
  const int removed = bleReceiver->clearBondedDevices();
  String message = "已清除 " + String(removed) + " 条 BLE 配对记录";
  message += "，并断开当前 BLE 客户端；Android 会自动重新连接。";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", buildStatusPage(message));
}

void NetworkManager::handleManualFirmwareUpload() {
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    manualUploadActive = false;
    manualUploadSucceeded = false;
    manualUploadError = "";
    manualUploadFilename = upload.filename;
    manualUploadWritten = 0;

    String lowerName = manualUploadFilename;
    lowerName.toLowerCase();
    if (webServer.arg("confirm") != "1") {
      failManualFirmwareUpload("缺少上传确认标记");
      return;
    }
    if (lowerName.isEmpty() || !lowerName.endsWith(".bin")) {
      failManualFirmwareUpload("请选择 .bin 固件文件");
      return;
    }
    if (otaManager != nullptr && otaManager->isBusy()) {
      failManualFirmwareUpload("在线 OTA 正在运行，请稍后再试");
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      failManualFirmwareUpload("无法开始写入：" + String(Update.errorString()));
      return;
    }
    manualUploadActive = true;
    Serial.printf("Manual firmware upload start: %s\n", manualUploadFilename.c_str());
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!manualUploadActive || !manualUploadError.isEmpty()) {
      return;
    }
    if (upload.currentSize == 0) {
      return;
    }
    if (manualUploadWritten == 0 && upload.buf[0] != 0xE9) {
      failManualFirmwareUpload("文件不是有效的 ESP32 固件镜像");
      return;
    }
    const size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      failManualFirmwareUpload("固件写入失败：" + String(Update.errorString()));
      return;
    }
    manualUploadWritten += written;
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!manualUploadActive || !manualUploadError.isEmpty()) {
      return;
    }
    if (manualUploadWritten < 4096) {
      failManualFirmwareUpload("固件文件过小");
      return;
    }
    if (!Update.end(true)) {
      failManualFirmwareUpload("固件校验失败：" + String(Update.errorString()));
      return;
    }
    manualUploadActive = false;
    manualUploadSucceeded = true;
    Serial.printf("Manual firmware upload complete: %u bytes\n",
                  static_cast<unsigned>(manualUploadWritten));
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    failManualFirmwareUpload("上传被中断");
  }
}

void NetworkManager::handleManualFirmwareUploadComplete() {
  webServer.sendHeader("Cache-Control", "no-store");
  if (!manualUploadSucceeded) {
    const String error = manualUploadError.isEmpty() ? String("未收到完整固件")
                                                      : manualUploadError;
    webServer.send(400, "text/html; charset=utf-8",
                   buildStatusPage("手动上传失败：" + error));
    return;
  }

  String page = F("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>固件上传成功</title></head><body style=\"font-family:sans-serif;padding:28px\">");
  page += F("<h2>固件上传成功</h2><p>已写入 ");
  page += String(manualUploadWritten);
  page += F(" bytes，设备将在 2 秒内重启。重启后请重新连接配置页。</p></body></html>");
  webServer.send(200, "text/html; charset=utf-8", page);
  manualRebootAt = millis() + 1800UL;
}

void NetworkManager::failManualFirmwareUpload(const String& message) {
  if (manualUploadActive) {
    Update.abort();
  }
  manualUploadActive = false;
  manualUploadSucceeded = false;
  manualUploadError = message;
  Serial.printf("Manual firmware upload failed: %s\n", message.c_str());
}

bool NetworkManager::applyOtaChannelSelection(String& message) {
  if (!otaManager || !webServer.hasArg("channel")) {
    return true;
  }

  String channel = webServer.arg("channel");
  channel.trim();
  if (channel.isEmpty()) {
    return true;
  }
  if (channel == otaManager->selectedChannel()) {
    return true;
  }
  if (!otaManager->setSelectedChannel(channel)) {
    message = otaManager->lastErrorText();
    return false;
  }
  return true;
}

void NetworkManager::handleStatusJson() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json; charset=utf-8", buildStatusJson());
}

void NetworkManager::handleTftBitmap() {
  if (!developerPreviewEnabled) {
    webServer.send(404, "text/plain; charset=utf-8", "TFT preview is disabled");
    return;
  }

  NavState empty;
  const NavState& state = navigationState ? *navigationState : empty;
  const unsigned long silenceMs = state.lastPacketAt == 0 ? ULONG_MAX : millis() - state.lastPacketAt;
  const bool dataConnected = isConnected() ||
                             (bleReceiver != nullptr && bleReceiver->isConnected());
  if (!tftPreview.sendBmp(webServer, state, dataConnected, silenceMs)) {
    webServer.send(503, "text/plain; charset=utf-8", "TFT preview framebuffer unavailable");
  }
}

void NetworkManager::handleNotFound() {
  if (portalActive) {
    redirectToPortal();
    return;
  }
  webServer.send(404, "text/plain; charset=utf-8", "未找到页面");
}

void NetworkManager::redirectToRoot() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.sendHeader("Location", "/", true);
  webServer.send(303, "text/plain; charset=utf-8", "正在返回首页...");
}

void NetworkManager::redirectToPortal() {
  webServer.sendHeader("Location", configPortalUrl(), true);
  webServer.send(302, "text/plain; charset=utf-8", "正在跳转到配网页面");
}

bool NetworkManager::shouldRedirectToPortal() {
  if (!isConfigPortalActive()) {
    return false;
  }

  String host = webServer.hostHeader();
  const int colon = host.indexOf(':');
  if (colon >= 0) {
    host = host.substring(0, colon);
  }
  return !host.isEmpty() && host != portalIp.toString();
}

String NetworkManager::buildStatusPage(const String& message) const {
  String page;
  page.reserve(30000);
  page += F("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>AMap ESP32 配置</title><style>");
  page += F(":root{color-scheme:light dark;--bg:#f2f2f7;--surface:rgba(255,255,255,.78);--solid:#fff;--raised:#f9f9fb;--ink:#1c1c1e;--muted:#6e6e73;--line:rgba(60,60,67,.14);--accent:#007aff;--accent-press:#0062cc;--green:#248a3d;--red:#d70015;--orange:#b25000;--shadow:0 16px 44px rgba(0,0,0,.08);--spring:cubic-bezier(.2,.8,.2,1)}*{box-sizing:border-box}html{background:var(--bg)}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text','Segoe UI','Microsoft YaHei',sans-serif;background:var(--bg);color:var(--ink);-webkit-font-smoothing:antialiased;-webkit-tap-highlight-color:transparent}");
  page += F("body:before{content:'';position:fixed;inset:0 0 auto;height:270px;pointer-events:none;background:radial-gradient(ellipse at 76% -20%,rgba(0,122,255,.15),transparent 62%)}main{position:relative;max-width:1080px;margin:0 auto;padding:38px 24px 64px}.hero{display:flex;align-items:flex-end;justify-content:space-between;gap:24px;margin:0 0 28px;padding:0 4px}.eyebrow{margin:0 0 7px;color:var(--accent);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}h1{font-size:clamp(30px,4vw,42px);line-height:1.04;letter-spacing:-.035em;margin:0}.sub{color:var(--muted);font-size:15px;line-height:1.45;margin:9px 0 0}.live-pill{display:inline-flex;align-items:center;gap:8px;min-height:34px;padding:7px 12px;border:1px solid var(--line);border-radius:999px;background:var(--surface);backdrop-filter:blur(20px) saturate(160%);-webkit-backdrop-filter:blur(20px) saturate(160%);font-size:13px;font-weight:650;white-space:nowrap}.live-dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 4px rgba(36,138,61,.12)}");
  page += F(".settings-layout{display:grid;grid-template-columns:210px minmax(0,1fr);gap:26px;align-items:start}.settings-sidebar{position:sticky;top:20px;padding:7px;border:1px solid rgba(255,255,255,.62);border-radius:20px;background:var(--surface);box-shadow:var(--shadow);backdrop-filter:blur(28px) saturate(180%);-webkit-backdrop-filter:blur(28px) saturate(180%)}.settings-sidebar strong{display:block;color:var(--muted);font-size:11px;letter-spacing:.08em;padding:13px 13px 8px}.nav-button{display:flex;align-items:center;width:100%;min-height:46px;margin:2px 0;padding:11px 13px;border:0;border-radius:14px;background:transparent;color:var(--ink);text-align:left;font:600 15px/1.2 inherit;cursor:pointer;touch-action:manipulation;transition:transform .12s ease-out,background .28s var(--spring),color .28s var(--spring)}.nav-button:hover{background:rgba(120,120,128,.10)}.nav-button:active{transform:scale(.975)}.nav-button.active{background:var(--accent);color:#fff}.settings-content{min-width:0}.settings-page{display:none}.settings-page.active{display:block;animation:page-in .34s var(--spring)}.section-heading{margin:0 0 16px;font-size:26px;line-height:1.15;letter-spacing:-.025em}.section-kicker{color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.1em;margin-bottom:7px}@keyframes page-in{from{opacity:0;transform:translateY(8px) scale(.995)}to{opacity:1;transform:none}}");
  page += F(".panel{background:var(--surface);border:1px solid rgba(255,255,255,.68);border-radius:22px;padding:20px;margin:14px 0;box-shadow:0 2px 12px rgba(0,0,0,.045);backdrop-filter:blur(22px) saturate(150%);-webkit-backdrop-filter:blur(22px) saturate(150%)}.grid{display:grid;grid-template-columns:132px 1fr;gap:0}.grid>div{padding:11px 2px;border-bottom:1px solid var(--line)}.grid>div:nth-last-child(-n+2){border-bottom:0}.k{color:var(--muted)}.v{font-weight:600;word-break:break-word}.ok{color:var(--green)}.bad{color:var(--red)}.msg{background:rgba(0,122,255,.09);border-color:rgba(0,122,255,.16);color:#064e9d}.err{background:rgba(255,59,48,.09);border-color:rgba(255,59,48,.16);color:var(--red)}");
  page += F("label{display:block;font-weight:600;margin:16px 0 7px}input,select{width:100%;min-height:48px;border:1px solid var(--line);border-radius:12px;padding:11px 13px;font:400 16px/1.2 inherit;color:var(--ink);background:var(--raised);outline:none;transition:border-color .16s ease,box-shadow .16s ease}input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,122,255,.16)}button{min-height:44px;border:0;border-radius:12px;background:var(--accent);color:#fff;font:650 15px/1.2 inherit;padding:11px 16px;margin-top:14px;cursor:pointer;touch-action:manipulation;transition:transform .1s ease-out,background .15s ease,opacity .15s ease}button:hover{background:var(--accent-press)}button:active{transform:scale(.975)}button:disabled{opacity:.45;cursor:not-allowed}button.secondary{background:rgba(120,120,128,.14);color:var(--ink)}.hint{font-size:13px;color:var(--muted);line-height:1.55}.notes{white-space:pre-wrap;font-weight:500;line-height:1.5}hr{border-color:var(--line)!important}");
  page += F(".dev-title{display:flex;justify-content:space-between;align-items:center;gap:12px}.dev-tag{font:700 11px/1 ui-monospace,'SFMono-Regular',monospace;letter-spacing:.07em;color:var(--accent)}.toggle{display:flex;align-items:center;gap:10px;min-height:44px;font-weight:600}.toggle input{width:20px;min-height:20px;accent-color:var(--accent);box-shadow:none}.tft-shell{margin-top:18px;padding:16px;border-radius:28px;background:#18181a;box-shadow:inset 0 0 0 1px #39393d,inset 0 0 0 7px #08080a,0 20px 46px rgba(0,0,0,.28)}.tft-glass{position:relative;overflow:hidden;width:100%;aspect-ratio:4/3;background:#000;border-radius:6px}.tft-glass:after{content:'';pointer-events:none;position:absolute;inset:0;background:linear-gradient(115deg,rgba(255,255,255,.045),transparent 28%,transparent 72%,rgba(255,255,255,.018));mix-blend-mode:screen}.tft-canvas{display:block;width:100%;height:100%;image-rendering:pixelated}.tft-caption{display:flex;justify-content:space-between;gap:12px;margin-top:12px;color:#8e8e93;font:650 11px/1.4 ui-monospace,'SFMono-Regular',monospace}.tft-caption span:last-child{text-align:right}.tft-live{color:#30d158}.tft-stale{color:#ff9f0a}.progress{margin-top:4px}.progress-track{height:8px;background:rgba(120,120,128,.18);border-radius:999px;overflow:hidden}.progress-bar{height:100%;width:0;background:var(--accent);border-radius:inherit;transition:width .3s var(--spring)}.progress-meta{display:flex;justify-content:space-between;gap:12px;margin-top:8px;font-size:13px;color:var(--muted)}");
  page += F("@media(prefers-color-scheme:dark){:root{--bg:#000;--surface:rgba(36,36,38,.78);--solid:#1c1c1e;--raised:#2c2c2e;--ink:#f5f5f7;--muted:#a1a1a6;--line:rgba(235,235,245,.12);--accent:#0a84ff;--accent-press:#409cff;--green:#30d158;--red:#ff453a;--orange:#ff9f0a;--shadow:0 18px 54px rgba(0,0,0,.42)}body:before{background:radial-gradient(ellipse at 76% -20%,rgba(10,132,255,.20),transparent 62%)}.settings-sidebar,.panel{border-color:rgba(255,255,255,.08)}.msg{color:#64aaff}.err{color:#ff6961}}@media(max-width:760px){body:before{height:200px}main{padding:22px 14px 44px}.hero{align-items:flex-start;flex-direction:column;gap:15px;margin-bottom:20px}.settings-layout{display:block}.settings-sidebar{top:0;z-index:20;display:flex;gap:4px;overflow-x:auto;margin:0 -4px 18px;padding:6px;border-radius:17px;scrollbar-width:none}.settings-sidebar::-webkit-scrollbar{display:none}.settings-sidebar strong{display:none}.nav-button{width:auto;min-height:42px;flex:0 0 auto;white-space:nowrap;margin:0;padding:10px 13px}.grid{grid-template-columns:112px 1fr}.panel{padding:17px;border-radius:19px}.tft-shell{padding:12px;border-radius:22px}}@media(prefers-reduced-motion:reduce){*,*:before,*:after{scroll-behavior:auto!important;animation:none!important;transition-duration:.01ms!important}}@media(prefers-reduced-transparency:reduce){.settings-sidebar,.panel,.live-pill{background:var(--solid);backdrop-filter:none;-webkit-backdrop-filter:none;border-color:var(--line)}}@media(prefers-contrast:more){.settings-sidebar,.panel,input,select{border:1px solid currentColor}.muted,.hint{color:var(--ink)}}</style></head><body><main>");
  page += F("<header class=\"hero\"><div><p class=\"eyebrow\">AMap · ESP32</p><h1>设备设置</h1><p class=\"sub\">连接、显示和固件，一处完成。</p></div><div class=\"live-pill\" role=\"status\"><span id=\"deviceStatusDot\" class=\"live-dot\"></span><span id=\"deviceStatusLabel\">");
  page += isConnected() ? F("Wi-Fi 已连接") : (isConfigPortalActive() ? F("配网热点已开启") : F("等待连接"));
  page += F("</span></div></header>");
  page += F("<div class=\"settings-layout\"><aside class=\"settings-sidebar\" aria-label=\"设备设置导航\"><strong>设备设置</strong><button type=\"button\" class=\"nav-button active\" data-target=\"overview\">总览</button><button type=\"button\" class=\"nav-button\" data-target=\"connection\">网络</button><button type=\"button\" class=\"nav-button\" data-target=\"display\">显示与 BLE</button><button type=\"button\" class=\"nav-button\" data-target=\"developer\">固件与开发</button></aside><div class=\"settings-content\"><section id=\"page-overview\" class=\"settings-page active\"><div class=\"section-kicker\">实时状态</div><h2 class=\"section-heading\">设备总览</h2>");

  const String visibleMessage = message.isEmpty() ? portalMessage : message;
  if (!visibleMessage.isEmpty()) {
    page += F("<section class=\"panel msg\">");
    page += htmlEscape(visibleMessage);
    page += F("</section>");
  }

  page += F("<section id=\"errorPanel\" class=\"panel err\" style=\"");
  page += lastError.isEmpty() ? F("display:none") : F("");
  page += F("\"><span id=\"lastError\">");
  page += htmlEscape(lastError);
  page += F("</span></section>");

  page += F("<section class=\"panel\"><div class=\"grid\">");
  page += F("<div class=\"k\">Wi-Fi 状态</div><div id=\"wifiStatus\" class=\"v ");
  page += isConnected() ? F("ok") : F("bad");
  page += F("\">");
  page += htmlEscape(wifiStatusName());
  page += F("</div><div class=\"k\">当前 SSID</div><div id=\"currentSsid\" class=\"v\">");
  page += htmlEscape(activeSsid.isEmpty() ? String("未配置") : activeSsid);
  page += F("</div><div class=\"k\">配置来源</div><div id=\"credentialSource\" class=\"v\">");
  page += htmlEscape(credentialSource);
  page += F("</div><div class=\"k\">STA IP</div><div id=\"staIp\" class=\"v\">");
  page += htmlEscape(isConnected() ? WiFi.localIP().toString() : String("未连接"));
  page += F("</div><div class=\"k\">UDP 端口</div><div id=\"udpPort\" class=\"v\">");
  page += String(AMAP_UDP_PORT);
  page += F("</div><div class=\"k\">屏幕芯片</div><div id=\"tftDriver\" class=\"v\">");
  page += hardwareSettings.tftDriverName();
  page += F("</div><div class=\"k\">触摸配置</div><div id=\"touchEnabled\" class=\"v\">");
  page += hardwareSettings.touchEnabled ? F("FT6336U 已启用") : F("无触摸");
  page += F("</div><div class=\"k\">屏幕反色</div><div id=\"invertColors\" class=\"v\">");
  page += hardwareSettings.invertColors ? F("已开启") : F("已关闭");
  page += F("</div><div class=\"k\">BLE 状态</div><div id=\"bleStatus\" class=\"v ");
  page += bleReceiver != nullptr && bleReceiver->isConnected() ? F("ok") : F("");
  page += F("\">");
  page += bleReceiver != nullptr && bleReceiver->isConnected() ? F("已连接") : F("等待连接");
  page += F("</div><div class=\"k\">BLE 名称</div><div id=\"bleName\" class=\"v\">");
  page += bleReceiver != nullptr ? htmlEscape(bleReceiver->deviceName()) : String("未初始化");
  page += F("</div><div class=\"k\">配对记录</div><div id=\"bleBondCount\" class=\"v\">");
  page += bleReceiver != nullptr ? String(bleReceiver->bondCount()) : String("0");
  page += F("</div><div class=\"k\">配网热点</div><div id=\"portalSsid\" class=\"v\">");
  page += isConfigPortalActive() ? htmlEscape(portalSsidName) : String("未启用");
  page += F("</div><div class=\"k\">配网页面</div><div id=\"portalUrl\" class=\"v\">");
  page += isConfigPortalActive() ? htmlEscape(configPortalUrl())
                                 : htmlEscape(String("http://") + WiFi.localIP().toString() + "/");
  page += F("</div></div></section>");
  page += F("</section>");

  page += F("<section id=\"page-connection\" class=\"settings-page\"><div class=\"section-kicker\">NETWORK</div><h2 class=\"section-heading\">连接设置</h2>");
  page += F("<section class=\"panel\"><form method=\"post\" action=\"/save\">");
  page += F("<label for=\"ssid\">Wi-Fi 名称</label><input id=\"ssid\" name=\"ssid\" required maxlength=\"32\" value=\"");
  page += htmlEscape(activeSsid);
  page += F("\"><label for=\"password\">Wi-Fi 密码</label>");
  page += F("<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\" placeholder=\"开放网络可留空\">");
  page += F("<button type=\"submit\">保存并连接</button></form>");
  page += F("<p class=\"hint\">保存后，ESP32 会在尝试连接新 Wi-Fi 时保留配网热点；STA 成功连上后，热点会自动关闭。</p>");
  page += F("</section>");

  page += F("<section class=\"panel\"><form method=\"post\" action=\"/clear\">");
  page += F("<button class=\"secondary\" type=\"submit\">清除已保存的 Wi-Fi</button>");
  page += F("</form><p class=\"hint\">清除后将回退到 Config.h 中的兜底 Wi-Fi；如果没有可用兜底配置，则保持在 AP 配网模式。</p></section>");
  page += F("</section>");

  page += F("<section id=\"page-display\" class=\"settings-page\"><div class=\"section-kicker\">HARDWARE</div><h2 class=\"section-heading\">显示与设备</h2>");
  page += F("<section class=\"panel\"><h2 style=\"font-size:18px;margin:0 0 8px\">显示硬件</h2>");
  page += F("<form method=\"post\" action=\"/hardware/display\" onsubmit=\"return confirm('保存后设备会自动重启，确认继续？')\">");
  page += F("<label for=\"tftDriverSelect\">屏幕控制芯片</label><select id=\"tftDriverSelect\" name=\"tftDriver\">");
  page += F("<option value=\"st7789\"");
  page += hardwareSettings.tftDriver == AMAP_TFT_DRIVER_ST7789 ? F(" selected") : F("");
  page += F(">ST7789V</option><option value=\"ili9341\"");
  page += hardwareSettings.tftDriver == AMAP_TFT_DRIVER_ILI9341 ? F(" selected") : F("");
  page += F(">ILI9341V</option></select>");
  page += F("<label class=\"toggle\"><input type=\"checkbox\" name=\"touchEnabled\" value=\"1\"");
  page += hardwareSettings.touchEnabled ? F(" checked") : F("");
  page += F(">启用 FT6336U 电容触摸</label>");
  page += F("<label class=\"toggle\"><input type=\"checkbox\" name=\"invertColors\" value=\"1\"");
  page += hardwareSettings.invertColors ? F(" checked") : F("");
  page += F(">启用屏幕反色</label>");
  page += F("<button type=\"submit\">保存显示硬件并重启</button></form>");
  page += F("<p class=\"hint\">启用触摸后：左右滑切换自动、导航、音乐和状态页，上滑进入状态页，下滑或长按返回自动模式。屏幕反色用于修正不同面板批次出现的颜色颠倒；这些设置都在开机阶段初始化，因此修改后必须重启。无触摸屏请关闭触摸，以免占用 GPIO8、GPIO9、GPIO17、GPIO18。</p></section>");

  page += F("<section class=\"panel\"><h2 style=\"font-size:18px;margin:0 0 8px\">BLE 管理</h2>");
  page += F("<form method=\"post\" action=\"/ble/clear\" onsubmit=\"return confirm('清除全部 BLE 配对记录并断开当前连接？')\">");
  page += F("<button class=\"secondary\" type=\"submit\">清除 BLE 配对设备</button></form>");
  page += F("<p class=\"hint\">当前版本使用免配对 GATT，通常不会保存 bond，因此数量一般为 0。此操作仍会删除全部 NimBLE bond、断开当前客户端并重新广播，Android 会自动重连。</p></section>");
  page += F("</section>");

  page += F("<section id=\"page-developer\" class=\"settings-page\"><div class=\"section-kicker\">FIRMWARE & LAB</div><h2 class=\"section-heading\">开发者选项</h2>");
  page += F("<section id=\"developerPreviewPanel\" class=\"panel\"><div class=\"dev-title\"><h2 style=\"font-size:18px;margin:0\">开发者选项</h2><span class=\"dev-tag\">TFT LAB</span></div>");
  page += F("<form method=\"post\" action=\"/developer/preview\"><label class=\"toggle\"><input type=\"checkbox\" name=\"enabled\" value=\"1\"");
  page += developerPreviewEnabled ? F(" checked") : F("");
  page += F(">启用 SPI TFT 模拟显示</label><button class=\"secondary\" type=\"submit\">保存开发者选项</button></form>");
  page += F("<p class=\"hint\">硬件数字孪生使用 320×240 横屏的实际坐标、RGB565 色板和渲染优先级；显示内容来自最近一次成功解析的 UDP/BLE JSON，不改变 OLED 输出。</p>");
  page += F("<div id=\"tftPreview\" class=\"tft-shell\"");
  page += developerPreviewEnabled ? F("") : F(" style=\"display:none\"");
  page += F("><div class=\"tft-glass\"><img id=\"tftFrame\" class=\"tft-canvas\" src=\"/tft.bmp\" alt=\"320x240 hardware frame\"></div><div class=\"tft-caption\"><span>");
  page += hardwareSettings.tftDriverName();
  page += F(" · 320×240 · ROTATION 1</span><span id=\"tftFreshness\" class=\"tft-stale\">等待 UDP</span></div></div></section>");

  page += F("<section class=\"panel\"><h2 style=\"font-size:18px;margin:0 0 12px\">OTA 更新</h2><div class=\"grid\">");
  page += F("<div class=\"k\">当前版本</div><div id=\"otaCurrent\" class=\"v\">");
  page += otaManager ? htmlEscape(otaManager->currentVersion() + " build " + String(otaManager->currentBuild()))
                     : String("未初始化");
  page += F("</div><div class=\"k\">运行渠道</div><div id=\"otaChannel\" class=\"v\">");
  page += otaManager ? htmlEscape(otaManager->currentChannel()) : String("-");
  page += F("</div><div class=\"k\">更新渠道</div><div id=\"otaSelectedChannel\" class=\"v\">");
  page += otaManager ? htmlEscape(otaManager->selectedChannel()) : String("-");
  page += F("</div><div class=\"k\">最新版本</div><div id=\"otaLatest\" class=\"v\">");
  page += otaManager ? htmlEscape(otaManager->latestBuildInfo()) : String("未检查");
  page += F("</div><div class=\"k\">OTA 状态</div><div id=\"otaStatus\" class=\"v\">");
  page += otaManager ? htmlEscape(otaManager->statusText()) : String("-");
  page += F("</div><div class=\"k\">更新日志</div><div id=\"otaNotes\" class=\"v notes\">");
  page += otaManager ? htmlEscape(otaManager->releaseNotes()) : String("");
  page += F("</div><div class=\"k\">错误信息</div><div id=\"otaError\" class=\"v bad\">");
  page += otaManager ? htmlEscape(otaManager->lastErrorText()) : String("");
  page += F("</div><div class=\"k\">升级进度</div><div class=\"v\"><div class=\"progress\"><div class=\"progress-track\"><div id=\"otaProgressBar\" class=\"progress-bar\" style=\"width:");
  page += otaManager ? String(otaManager->progressPercent()) : String("0");
  page += F("%\"></div></div><div class=\"progress-meta\"><span id=\"otaProgressText\">");
  page += otaManager ? htmlEscape(otaManager->progressText()) : String("未开始");
  page += F("</span><span id=\"otaProgressPercent\">");
  page += otaManager ? String(otaManager->progressPercent()) + "%" : String("0%");
  page += F("</span></div></div></div></div>");
  page += F("<form method=\"post\" action=\"/ota/check\" style=\"display:inline\">");
  page += F("<label for=\"otaChannelSelect\">更新渠道</label><select id=\"otaChannelSelect\" name=\"channel\">");
  page += F("<option value=\"stable\"");
  page += otaManager && otaManager->selectedChannel() == "stable" ? F(" selected") : F("");
  page += F(">stable</option><option value=\"dev\"");
  page += otaManager && otaManager->selectedChannel() == "dev" ? F(" selected") : F("");
  page += F(">dev</option></select>");
  page += F("<button type=\"submit\">检查更新</button></form>");
  page += F("<form method=\"post\" action=\"/ota/upgrade\" style=\"display:inline;margin-left:8px\">");
  page += F("<input id=\"otaUpgradeChannel\" type=\"hidden\" name=\"channel\" value=\"");
  page += otaManager ? htmlEscape(otaManager->selectedChannel()) : String("stable");
  page += F("\"><button type=\"submit\">立即升级</button></form>");
  page += F("<p class=\"hint\">所选更新渠道可以与当前运行固件的渠道不同。更新为手动触发；manifest 与固件 URL 遇到重定向时会自动跟随。</p>");
  page += F("<hr style=\"border:0;border-top:1px solid #dce3ee;margin:20px 0\"><h3 style=\"font-size:16px;margin:0 0 8px\">手动上传固件</h3>");
  page += F("<form method=\"post\" action=\"/firmware/upload?confirm=1\" enctype=\"multipart/form-data\" onsubmit=\"return confirm('确认写入所选固件并重启设备？请勿在上传过程中断电。')\">");
  page += F("<input type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\" required><button type=\"submit\">上传并安装 .bin</button></form>");
  page += F("<p class=\"hint\">仅接受 ESP32 应用固件 .bin，最大可用空间约 ");
  page += String(ESP.getFreeSketchSpace() / 1024U);
  page += F(" KB。上传完成后设备自动重启；该方式不校验远端 manifest 或 SHA256，请只使用可信固件。配置页没有额外登录验证，请仅在可信 Wi-Fi 上使用；若启用 AP 配网，请为热点设置密码。</p></section>");
  page += F("</section></div></div>");

  page += F("<script>(function(){function e(i){return document.getElementById(i)}var mobile=location.search.indexOf('mobile=1')>=0;if(mobile){var dp=e('developerPreviewPanel');if(dp)dp.remove();var sub=document.querySelector('.sub');if(sub)sub.textContent='手机端设备控制 · TFT 模拟显示已隐藏'}function nav(){var bs=document.querySelectorAll('.nav-button');for(var i=0;i<bs.length;i++){bs[i].setAttribute('aria-selected',bs[i].classList.contains('active')?'true':'false');bs[i].addEventListener('click',function(){for(var j=0;j<bs.length;j++){bs[j].classList.remove('active');bs[j].setAttribute('aria-selected','false')}this.classList.add('active');this.setAttribute('aria-selected','true');var ps=document.querySelectorAll('.settings-page');for(var k=0;k<ps.length;k++)ps[k].classList.remove('active');var p=e('page-'+this.getAttribute('data-target'));if(p)p.classList.add('active')})}}function t(i,v){var n=e(i);if(n)n.textContent=v||''}function sc(){var c=e('otaChannelSelect');var h=e('otaUpgradeChannel');if(c&&h)h.value=c.value||'stable'}function bc(){var c=e('otaChannelSelect');if(!c||c.dataset.bound)return;c.dataset.bound='1';c.addEventListener('change',function(){c.dataset.dirty='1';c.dataset.pending=c.value||'stable';sc()});c.addEventListener('input',function(){c.dataset.dirty='1';c.dataset.pending=c.value||'stable';sc()});sc()}");
  page += F("function d(v,f){return v===undefined||v===null||v===''?f:v}function tc(s){return s===10?'#666':s===0?'#2196f3':s===1?'#1abf54':s===2?'#ffd600':s===3?'#ff1744':s===4?'#b71c1c':s===5?'#007d5d':'#333'}function tm(n){var x=(n||{}).tmc||{},a=x.segments||[],b=e('tftTmcSegments'),m=e('tftTmcMarker'),l=e('tftTmcLabel');if(!b)return;b.textContent='';if(!a.length||!(x.totalDistance>0)){if(l)l.textContent='等待数据';if(m)m.style.display='none';return}var sum=0;for(var i=0;i<a.length;i++)sum+=Math.max(0,Number(a[i].distance)||0);if(!(sum>0))sum=x.totalDistance;for(var j=0;j<a.length;j++){var q=document.createElement('i');q.style.flex=String(Math.max(0,Number(a[j].distance)||0));q.style.background=tc(Number(a[j].status));b.appendChild(q)}var p=Math.max(0,Math.min(100,(Number(x.finishDistance)||0)/Number(x.totalDistance)*100));if(m){m.style.left=String(p)+'%';m.style.display='block'}if(l)l.textContent=String(a.length)+' 段 · '+Math.round(p)+'%'}function pv(n){n=n||{};if(!n.active){t('tftMode','WAIT');t('tftRoad','等待 UDP JSON');t('tftSpeed','--');t('tftArrow','·');t('tftTurn','尚未接收到有效导航帧');t('tftNext','请从 Android 转发器发送测试帧');t('tftEta','--');t('tftDestination','--');t('tftGuide','--');t('tftTraffic','--');t('tftAlert','等待 UDP JSON');var z=e('tftProgress');if(z)z.style.width='0%';tm({});return}var r=n.route||{},g=n.guide||{},ri=n.roadInfo||{},u=n.turn||{},s=n.speed||{};var icon=Number(u.icon||0),a=(icon===2||icon===4||icon===6||icon===18)?'←':((icon===8)?'↻':((icon===3||icon===5||icon===7||icon===19)?'→':'↑'));t('tftMode',String(d(n.mode,'nav')).toUpperCase());t('tftRoad',d(n.road,'--'));t('tftSpeed',s.current>=0?s.current:'--');t('tftArrow',a);t('tftTurn',d((d(u.text,'直行'))+' '+d(u.distanceText,''),'直行'));t('tftNext',d(u.road,'--'));t('tftEta',(d((n.eta||{}).remainTimeText,'--'))+' · '+d((n.eta||{}).remainDistanceText,'--'));t('tftDestination',d(r.destination,'--'));t('tftGuide',d(g.exitName?g.exitName+(g.exitDirection?' · '+g.exitDirection:''):(g.serviceAreaName?'服务区 '+g.serviceAreaName+' '+d(g.serviceAreaDistance,''):'--'),'--'));t('tftTraffic',(d(ri.type,'--'))+(ri.traffic?' · '+ri.traffic:'')+(ri.crossMap?' · 路口放大图':''));t('tftAlert',d(n.alert,d(n.lightText,'--')));var p=Number(r.progressPercent);if(!(p>=0&&p<=100))p=0;var b=e('tftProgress');if(b)b.style.width=String(p)+'%';tm(n)}");
  page += F("function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(!s)return;var ds=e('deviceStatusLabel'),dd=e('deviceStatusDot'),dc=s.connected?'var(--green)':(s.portalActive?'var(--orange)':'var(--red)'),sh=s.connected?'rgba(36,138,61,.14)':(s.portalActive?'rgba(178,80,0,.14)':'rgba(215,0,21,.14)');if(ds)ds.textContent=s.connected?'Wi-Fi 已连接':(s.portalActive?'配网热点已开启':'等待连接');if(dd){dd.style.background=dc;dd.style.boxShadow='0 0 0 4px '+sh}");
  page += F("var w=e('wifiStatus');if(w){w.textContent=s.wifiStatus||'';w.className='v '+(s.connected?'ok':'bad')}");
  page += F("t('currentSsid',s.ssid||'未配置');t('credentialSource',s.source||'none');t('staIp',s.staIp||'未连接');t('udpPort',String(s.udpPort||''));");
  page += F("var bs=e('bleStatus');if(bs){bs.textContent=s.bleConnected?'已连接':'等待连接';bs.className='v '+(s.bleConnected?'ok':'')}t('bleName',s.bleName||'未初始化');t('bleBondCount',String(s.bleBondCount||0));");
  page += F("t('portalSsid',s.portalActive?s.portalSsid:'未启用');t('portalUrl',s.portalActive?s.portalUrl:(s.staIp?('http://'+s.staIp+'/'):'未连接'));");
  page += F("if(s.ota){t('otaCurrent',s.ota.currentVersion+' build '+s.ota.currentBuild);t('otaChannel',s.ota.currentChannel);t('otaSelectedChannel',s.ota.selectedChannel||'');t('otaLatest',s.ota.latestBuildInfo||'未检查');t('otaStatus',s.ota.status);t('otaNotes',s.ota.changelog||s.ota.releaseNotes||'');t('otaError',s.ota.lastError||'');var c=e('otaChannelSelect');if(c&&s.ota.selectedChannel)c.value=s.ota.selectedChannel;var h=document.querySelector('form[action=\"/ota/upgrade\"] input[name=\"channel\"]');if(h&&s.ota.selectedChannel)h.value=s.ota.selectedChannel}");
  page += F("if(s.ota){var pb=e('otaProgressBar');if(pb)pb.style.width=String(s.ota.progressPercent||0)+'%';t('otaProgressText',s.ota.progressText||'未开始');t('otaProgressPercent',String(s.ota.progressPercent||0)+'%');var busy=!!s.ota.busy;var cb=document.querySelector('form[action=\"/ota/check\"] button');if(cb)cb.disabled=busy;var ub=document.querySelector('form[action=\"/ota/upgrade\"] button');if(ub)ub.disabled=busy;}");
  page += F("if(s.developerPreview){var tf=e('tftFreshness'),n=s.nav||{},age=Number(n.packetAgeMs),cfg=s.tft||{},stale=Number(cfg.staleMs)||3000,wait=Number(cfg.standbyMs)||10000,data=!!(s.connected||s.bleConnected);if(tf){if(!data){tf.textContent='waiting for UDP/BLE';tf.className='tft-stale'}else if(!n.active||age<0||age>wait){tf.textContent='waiting for navigation';tf.className='tft-stale'}else if(age>stale){tf.textContent='phone data stale '+age+'ms';tf.className='tft-stale'}else{tf.textContent=(s.bleConnected?'BLE':'UDP')+' live · '+age+'ms';tf.className='tft-live'}}}");
  page += F("var c2=e('otaChannelSelect');if(c2&&c2.dataset.dirty&&c2.dataset.pending){if(s.ota&&c2.dataset.pending===s.ota.selectedChannel){c2.dataset.dirty='';c2.dataset.pending=''}else{c2.value=c2.dataset.pending;sc()}}");
  page += F("var p=e('errorPanel');var l=e('lastError');if(p&&l){l.textContent=s.lastError||'';p.style.display=s.lastError?'':'none'}}).catch(function(){})}");
  page += F("nav();bc();setInterval(poll,800);setTimeout(poll,200)})();</script>");
  page += F(R"BMP(<script>(function(){var frame=document.getElementById('tftFrame');if(!frame)return;function refresh(){frame.src='/tft.bmp?frame='+Date.now()}setInterval(refresh,1000)})();</script>)BMP");
#if 0  // Superseded SVG and canvas preview implementation.
  page += F("<script>(function(){function e(i){return document.getElementById(i)}function g(c){var U='&#8593;',L='&#8592;',R='&#8594;',UL='&#8624;',UR='&#8625;',a=[U,L,L+U,R,U+R,UL,L+R,L+U+R,UR,UL+U,U+UR,UL+L,R+UR,'/'+U,'/'+U,U,L,L+U,R,U+R,UL,L+R,L+U+R,UR,UL+U,U+UR,UL+L,R+UR,'/'+U,'/'+U,U+L,U+L,U+R,U+R,L+R,L+R,L+U+R,L+U+R,L+U+R,UL+U,UL+U,U+UR,U+UR,L+UL,L+UL,R+UR,R+UR,'/'+L+UR,L+UL];return c===62?L+U+R:(c===85?'/'+U:(c===89?'-':(a[c]||U)))}function draw(n){var x=(n||{}).lane||{},a=x.lanes||[],r=x.advised||[],b=e('tftLanes'),h=e('tftTmcSegments');if(!b&&h){b=document.createElement('div');b.id='tftLanes';b.className='tft-lanes';h.parentNode.parentNode.insertBefore(b,h.parentNode.previousSibling)}if(!b)return;b.textContent='';for(var i=0;i<a.length&&i<8;i++){var q=document.createElement('span'),c=Number(a[i]);q.className='tft-lane '+((r[i]||(c>=15&&c<=48))?'on':'');q.innerHTML=g(c);b.appendChild(q)}}function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(s&&s.developerPreview)draw(s.nav)}).catch(function(){})}setInterval(poll,850);setTimeout(poll,240)})()</script>");
  page += F("<script>(function(){function e(i){return document.getElementById(i)}function da(d){return d===0?'↻':d===1?'←':(d===2||d===3)?'→':d===4?'↑':d===5||d===6?'↖':d===7||d===8?'↗':'·'}function draw(n){var b=e('tftLights'),a=(n||{}).lights||[];if(!b)return;b.textContent='';for(var i=0;i<a.length&&i<4;i++){var x=a[i]||{},s=Number(x.status),q=document.createElement('span');q.className='tft-light '+(s===1?'red':s===4?'green':'yellow');var c=document.createElement('i');c.textContent=da(Number(x.dir));var v=document.createElement('b');v.textContent=String(Math.max(0,Number(x.seconds)||0));var u=document.createElement('small');u.textContent=s===1?'红':s===4?'绿':'黄';q.appendChild(c);q.appendChild(v);q.appendChild(u);b.appendChild(q)}}function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(s&&s.developerPreview)draw(s.nav)}).catch(function(){})}setInterval(poll,800);setTimeout(poll,210)})()</script>");
  page += F(R"TFT(<script>(function(){
var cv=document.getElementById('tftCanvas'),g=cv&&cv.getContext('2d');if(!g)return;
function rgb(v){var r=(v>>11)&31,m=(v>>5)&63,b=v&31;return'rgb('+Math.round(r*255/31)+','+Math.round(m*255/63)+','+Math.round(b*255/31)+')'}
var C={bg:rgb(0x0861),panel:rgb(0x10a2),cyan:rgb(0x55ff),text:rgb(0xffff),muted:rgb(0x9cd3),yellow:rgb(0xfea0),gray:rgb(0x5aeb),red:rgb(0xd1a4),green:rgb(0x2589),head:rgb(0x0248),border:rgb(0x4268),prog:rgb(0x2945)};
function rr(x,y,w,h,r,fill,stroke){g.beginPath();g.moveTo(x+r,y);g.arcTo(x+w,y,x+w,y+h,r);g.arcTo(x+w,y+h,x,y+h,r);g.arcTo(x,y+h,x,y,r);g.arcTo(x,y,x+w,y,r);g.closePath();if(fill){g.fillStyle=fill;g.fill()}if(stroke){g.strokeStyle=stroke;g.stroke()}}
function cut(s,n){s=String(s||'');return s.length>n?s.substring(0,Math.max(0,n-1))+'…':s}function tx(x,y,s,c,n){g.font='12px "Microsoft YaHei","Noto Sans CJK SC",sans-serif';g.textBaseline='alphabetic';g.fillStyle=c;g.fillText(n?cut(s,n):String(s||''),x,y)}
function arrow(icon){g.strokeStyle=C.cyan;g.fillStyle=C.cyan;g.lineWidth=5;g.lineCap='square';g.beginPath();if(icon===8){g.arc(62,86,24,.55,5.55);g.stroke();g.beginPath();g.moveTo(39,73);g.lineTo(38,51);g.lineTo(58,59);g.fill()}else{var l=icon===2||icon===4||icon===6||icon===18,r=icon===3||icon===5||icon===7||icon===19;g.moveTo(62,126);g.lineTo(62,84);if(l)g.lineTo(icon===2||icon===18?28:35,84);else if(r)g.lineTo(icon===3||icon===19?96:89,84);else g.lineTo(62,50);g.stroke();var ex=l?(icon===2||icon===18?28:35):r?(icon===3||icon===19?96:89):62,ey=l||r?84:50;g.beginPath();if(l){g.moveTo(ex,ey);g.lineTo(ex+18,ey-12);g.lineTo(ex+18,ey+12)}else if(r){g.moveTo(ex,ey);g.lineTo(ex-18,ey-12);g.lineTo(ex-18,ey+12)}else{g.moveTo(ex,ey);g.lineTo(ex-12,ey+18);g.lineTo(ex+12,ey+18)}g.closePath();g.fill()}}
function lightArrow(dir,x,y){g.strokeStyle=C.text;g.lineWidth=1;g.beginPath();if(dir===0){g.arc(x,y,4,0,7);g.moveTo(x-4,y);g.lineTo(x-1,y-4)}else{var ex=x,ey=y-5;if(dir===1||dir===5||dir===6){ex=x-5;ey=dir===1?y:y-5}else if(dir===2||dir===3||dir===7||dir===8){ex=x+5;ey=(dir===2||dir===3)?y:y-5}g.moveTo(x,y+4);g.lineTo(x,y);g.lineTo(ex,ey);g.moveTo(ex,ey);g.lineTo(ex+(ex>x?-2:ex<x?2:-2),ey+2);if(ex===x)g.lineTo(ex+2,ey+2);else{g.moveTo(ex,ey);g.lineTo(ex,ey+3)}}g.stroke()}
function lights(a){a=(a||[]).slice(0,4);if(!a.length)return;var cell=Math.max(30,Math.floor(186/a.length));for(var i=0;i<a.length;i++){var z=a[i]||{},left=124+i*cell,cx=left+10,cy=134,s=Number(z.status),c=s===1?C.red:s===4?C.green:C.yellow;g.fillStyle=c;g.beginPath();g.arc(cx,cy,9,0,7);g.fill();lightArrow(Number(z.dir),cx,cy);tx(left+22,139,Math.max(0,Number(z.seconds)||0),c)}}
function tmc(n){rr(12,169,296,8,4,null,C.border);var a=(n.tmc||{}).segments||[],total=Number((n.tmc||{}).totalDistance);if(!a.length||!(total>0))return;var colors=[rgb(0x2196),rgb(0x05e6),rgb(0xffe0),rgb(0xf8a0),rgb(0xb800),rgb(0x03ef)],sum=0;for(var i=0;i<a.length;i++)sum+=Math.max(0,Number(a[i].distance)||0);if(!(sum>0))sum=total;var cur=13;for(var j=0;j<a.length;j++){var w=Math.max(1,Math.floor(294*Math.max(0,Number(a[j].distance)||0)/sum));g.fillStyle=colors[Number(a[j].status)]||C.gray;g.fillRect(cur,170,w,6);cur+=w}var f=Math.max(0,Math.min(total,Number((n.tmc||{}).finishDistance)||0)),m=13+Math.floor(294*f/total);g.fillStyle=C.text;g.beginPath();g.moveTo(m-4,165);g.lineTo(m+4,165);g.lineTo(m,170);g.fill()}
function lane(code,ad,left,w){var map={0:[1,0],1:[2,0],2:[3,0],3:[4,0],4:[5,0],5:[8,0],6:[6,0],7:[7,0],8:[16,0],9:[9,0],10:[17,0],11:[10,0],12:[20,0],13:[33,0],14:[33,0],15:[1,1],16:[2,2],17:[3,3],18:[4,4],19:[5,5],20:[8,8],21:[6,6],22:[7,7],23:[16,16],24:[9,9],25:[17,17],26:[10,10],27:[20,20],28:[33,33],29:[33,33],30:[3,1],31:[3,2],32:[5,1],33:[5,4],34:[6,2],35:[6,4],36:[7,1],37:[7,2],38:[7,4],39:[9,1],40:[9,8],41:[17,1],42:[17,16],43:[10,2],44:[10,8],45:[20,4],46:[20,16],47:[50,16],48:[10,8],62:[7,7],85:[33,1],89:[0,0]},q=map[code]||[1,1],d=q[0],on=q[1],cx=left+Math.floor(w/2),base=218,split=206;g.lineWidth=1;rr(left,187,w,37,5,null,ad?C.cyan:C.border);function p(bit,ex,ey){if(!(d&bit))return;g.strokeStyle=((on&bit)||ad)?C.text:C.gray;g.beginPath();g.moveTo(cx,base);g.lineTo(cx,split);g.lineTo(ex,ey);g.stroke();g.fillStyle=g.strokeStyle;g.beginPath();g.arc(ex,ey,2,0,7);g.fill()}p(1,cx,194);p(2,left+7,197);p(4,left+w-8,197);p(8,left+9,210);p(16,left+w-10,210);if(d&32){g.strokeStyle=C.gray;g.beginPath();g.moveTo(left+5,195);g.lineTo(left+w-5,195);g.stroke()}}
function lanes(n){var a=((n.lane||{}).lanes||[]),b=((n.lane||{}).advised||[]);if(!a.length)return;var cell=Math.min(42,Math.floor(296/a.length)),start=Math.floor((320-cell*a.length)/2);for(var i=0;i<a.length;i++)lane(Number(a[i]),!!b[i],start+i*cell,cell-3)}
function standby(a,b){g.fillStyle=C.bg;g.fillRect(0,0,320,240);rr(16,40,288,154,14,C.panel);tx(32,82,a,C.cyan);tx(32,112,b,C.text);tx(32,145,'ST7789 240x320 / ESP32-S3',C.muted)}
function nav(n){g.fillStyle=C.bg;g.fillRect(0,0,320,240);g.fillStyle=C.head;g.fillRect(0,0,320,28);tx(12,19,n.mode==='cruise'?'CRUISE':'NAVIGATION',C.cyan);tx(102,19,n.road,C.text,29);rr(10,38,102,104,12,C.panel);arrow(Number((n.turn||{}).icon));tx(124,64,(n.turn||{}).text,C.text,26);tx(124,92,(n.turn||{}).distanceText,C.yellow);tx(124,120,(n.turn||{}).road||n.road,C.muted,26);lights(n.lights);var p=Number((n.route||{}).progressPercent);if(!(p>=0&&p<=100))p=0;rr(12,153,296,5,3,C.prog);if(p>0)rr(12,153,Math.floor(296*p/100),5,3,C.cyan);tmc(n);lanes(n);g.fillStyle=C.head;g.fillRect(0,228,320,12);var eta=((n.eta||{}).remainTimeText||'')+'  '+((n.eta||{}).remainDistanceText||'');if(Number((n.speed||{}).current)>=0)eta+='  '+Number((n.speed||{}).current)+'km/h';tx(8,239,eta,C.text,43);var ca=n.camera||{},dist=Number(ca.distance),lim=Number(ca.speedLimit)>0?Number(ca.speedLimit):Number((n.speed||{}).limit);if(dist>=0||lim>0){g.strokeStyle=C.yellow;g.strokeRect(286,151,12,12);g.beginPath();g.arc(292,157,3,0,7);g.stroke();var ct='Camera'+(dist>=0?' '+dist+'m':'')+(lim>0?'  Limit '+lim:'');tx(12,181,ct,C.yellow,38)}}
function draw(s){var n=s.nav||{},age=Number(n.packetAgeMs),cfg=s.tft||{},stale=Number(cfg.staleMs)||3000,wait=Number(cfg.standbyMs)||10000,label=e('tftFreshness');if(!s.connected){standby('AMap ESP32','AP configuration mode');label.textContent='Wi-Fi disconnected';label.className='tft-stale'}else if(!n.active||age<0||age>wait){standby('AMap ESP32','Waiting for navigation JSON');label.textContent='waiting for navigation';label.className='tft-stale'}else if(age>stale){standby('Navigation paused','Waiting for phone data');label.textContent='phone data stale '+age+'ms';label.className='tft-stale'}else{nav(n);label.textContent='UDP live · '+age+'ms';label.className='tft-live'}}
function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(s&&s.developerPreview)draw(s)}).catch(function(){})}setInterval(poll,800);setTimeout(poll,200)})();</script>)TFT");
  page += F(R"SVG(<script>(function(){
var v=document.getElementById('tftSvg');if(!v)return;function c(n){var r=(n>>11)&31,g=(n>>5)&63,b=n&31;return'rgb('+Math.round(r*255/31)+','+Math.round(g*255/63)+','+Math.round(b*255/31)+')'}var K={b:c(0x0861),p:c(0x10a2),y:c(0x55ff),t:c(0xffff),m:c(0x9cd3),a:c(0xfea0),g:c(0x5aeb),r:c(0xd1a4),n:c(0x2589),h:c(0x0248),o:c(0x4268),q:c(0x2945)};
function esc(s){return String(s||'').replace(/[&<>"']/g,function(x){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[x]})}function cut(s,n){s=String(s||'');return s.length>n?s.substring(0,Math.max(0,n-1))+'…':s}function R(x,y,w,h,r,f,o){return'<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'" rx="'+r+'" fill="'+(f||'none')+'"'+(o?' stroke="'+o+'"':'')+'/>'}function T(x,y,s,col,n){return'<text x="'+x+'" y="'+y+'" fill="'+col+'" font-family="Microsoft YaHei,Noto Sans CJK SC,sans-serif" font-size="12">'+esc(n?cut(s,n):s)+'</text>'}
function turn(i){if(i===8)return'<path d="M62 126V110A24 24 0 1 0 38 86M38 72V51L58 59" fill="none" stroke="'+K.y+'" stroke-width="5" stroke-linecap="square" stroke-linejoin="miter"/>';var l=i===2||i===4||i===6||i===18,r=i===3||i===5||i===7||i===19,ex=l?(i===2||i===18?28:35):r?(i===3||i===19?96:89):62,ey=l||r?84:50,d='M62 126V84'+(l?'H'+ex:r?'H'+ex:'V50');var head=l?'M'+ex+' '+ey+'l18 -12v24z':r?'M'+ex+' '+ey+'l-18 -12v24z':'M62 50l-12 18h24z';return'<path d="'+d+'" fill="none" stroke="'+K.y+'" stroke-width="5" stroke-linecap="square"/><path d="'+head+'" fill="'+K.y+'"/>'}
function larr(a){a=(a||[]).slice(0,4);if(!a.length)return'';var z='',w=Math.max(30,Math.floor(186/a.length));for(var i=0;i<a.length;i++){var q=a[i]||{},x=124+i*w+10,y=134,s=Number(q.status),co=s===1?K.r:s===4?K.n:K.a,d=Number(q.dir),ex=x,ey=y-5;if(d===1||d===5||d===6){ex=x-5;ey=d===1?y:y-5}else if(d===2||d===3||d===7||d===8){ex=x+5;ey=(d===2||d===3)?y:y-5}var ar=d===0?'<circle cx="'+x+'" cy="'+y+'" r="4" fill="none" stroke="'+K.t+'"/><path d="M'+(x-4)+' '+y+'l3 -4" stroke="'+K.t+'" fill="none"/>':'<path d="M'+x+' '+(y+4)+'V'+y+'L'+ex+' '+ey+' M'+ex+' '+ey+'l'+(ex>x?-2:ex<x?2:-2)+' 2" stroke="'+K.t+'" fill="none"/>';z+='<circle cx="'+x+'" cy="'+y+'" r="9" fill="'+co+'"/>'+ar+T(x+12,y+5,Math.max(0,Number(q.seconds)||0),co)}return z}
var M={0:[1,0],1:[2,0],2:[3,0],3:[4,0],4:[5,0],5:[8,0],6:[6,0],7:[7,0],8:[16,0],9:[9,0],10:[17,0],11:[10,0],12:[20,0],13:[33,0],14:[33,0],15:[1,1],16:[2,2],17:[3,3],18:[4,4],19:[5,5],20:[8,8],21:[6,6],22:[7,7],23:[16,16],24:[9,9],25:[17,17],26:[10,10],27:[20,20],28:[33,33],29:[33,33],30:[3,1],31:[3,2],32:[5,1],33:[5,4],34:[6,2],35:[6,4],36:[7,1],37:[7,2],38:[7,4],39:[9,1],40:[9,8],41:[17,1],42:[17,16],43:[10,2],44:[10,8],45:[20,4],46:[20,16],47:[50,16],48:[10,8],62:[7,7],85:[33,1],89:[0,0]};
function lanes(n){var a=((n.lane||{}).lanes||[]),b=((n.lane||{}).advised||[]);if(!a.length)return'';var z='',w=Math.min(42,Math.floor(296/a.length)),st=Math.floor((320-w*a.length)/2);for(var i=0;i<a.length;i++){var q=M[Number(a[i])]||[1,1],d=q[0],on=q[1],left=st+i*w,cx=left+Math.floor((w-3)/2),ww=w-3,ad=!!b[i],edge=ad?K.y:K.o;z+=R(left,187,ww,37,5,null,edge);function p(bit,ex,ey){if(!(d&bit))return;var co=((on&bit)||ad)?K.t:K.g;z+='<path d="M'+cx+' 218V206L'+ex+' '+ey+'" stroke="'+co+'" fill="none"/><circle cx="'+ex+'" cy="'+ey+'" r="2" fill="'+co+'"/>'}p(1,cx,194);p(2,left+7,197);p(4,left+ww-8,197);p(8,left+9,210);p(16,left+ww-10,210);if(d&32)z+='<path d="M'+(left+5)+' 195H'+(left+ww-5)+'" stroke="'+K.g+'"/>'}return z}
function tmc(n){var a=(n.tmc||{}).segments||[],total=Number((n.tmc||{}).totalDistance),z=R(12,169,296,8,4,null,K.o);if(!a.length||!(total>0))return z;var cols=[c(0x2196),c(0x05e6),c(0xffe0),c(0xf8a0),c(0xb800),c(0x03ef)],sum=0;for(var i=0;i<a.length;i++)sum+=Math.max(0,Number(a[i].distance)||0);if(!(sum>0))sum=total;var cur=13;for(var j=0;j<a.length;j++){var w=Math.max(1,Math.floor(294*Math.max(0,Number(a[j].distance)||0)/sum));z+='<rect x="'+cur+'" y="170" width="'+w+'" height="6" fill="'+(cols[Number(a[j].status)]||K.g)+'"/>';cur+=w}var f=Math.max(0,Math.min(total,Number((n.tmc||{}).finishDistance)||0)),m=13+Math.floor(294*f/total);return z+'<path d="M'+(m-4)+' 165H'+(m+4)+'L'+m+' 170z" fill="'+K.t+'"/>'}
function standby(a,b){v.innerHTML='<rect width="320" height="240" fill="'+K.b+'"/>'+R(16,40,288,154,14,K.p)+T(32,82,a,K.y)+T(32,112,b,K.t)+T(32,145,'ST7789 240x320 / ESP32-S3',K.m)}
function nav(n){var z='<rect width="320" height="240" fill="'+K.b+'"/><rect width="320" height="28" fill="'+K.h+'"/>'+T(12,19,n.mode==='cruise'?'CRUISE':'NAVIGATION',K.y)+T(102,19,n.road,K.t,29)+R(10,38,102,104,12,K.p)+turn(Number((n.turn||{}).icon))+T(124,64,(n.turn||{}).text,K.t,26)+T(124,92,(n.turn||{}).distanceText,K.a)+T(124,120,(n.turn||{}).road||n.road,K.m,26)+larr(n.lights);var p=Number((n.route||{}).progressPercent);if(!(p>=0&&p<=100))p=0;z+=R(12,153,296,5,3,K.q)+(p>0?R(12,153,Math.floor(296*p/100),5,3,K.y):'')+tmc(n)+lanes(n)+'<rect x="0" y="228" width="320" height="12" fill="'+K.h+'"/>';var eta=((n.eta||{}).remainTimeText||'')+'  '+((n.eta||{}).remainDistanceText||'');if(Number((n.speed||{}).current)>=0)eta+='  '+Number((n.speed||{}).current)+'km/h';z+=T(8,239,eta,K.t,43);var ca=n.camera||{},dist=Number(ca.distance),lim=Number(ca.speedLimit)>0?Number(ca.speedLimit):Number((n.speed||{}).limit);if(dist>=0||lim>0){z+='<rect x="286" y="151" width="12" height="12" fill="none" stroke="'+K.a+'"/><circle cx="292" cy="157" r="3" fill="none" stroke="'+K.a+'"/>';z+=T(12,181,'Camera'+(dist>=0?' '+dist+'m':'')+(lim>0?'  Limit '+lim:''),K.a,38)}v.innerHTML=z}
function paint(s){var n=s.nav||{},age=Number(n.packetAgeMs),q=s.tft||{},stale=Number(q.staleMs)||3000,wait=Number(q.standbyMs)||10000,l=document.getElementById('tftFreshness');if(!s.connected){standby('AMap ESP32','AP configuration mode');l.textContent='Wi-Fi disconnected';l.className='tft-stale'}else if(!n.active||age<0||age>wait){standby('AMap ESP32','Waiting for navigation JSON');l.textContent='waiting for navigation';l.className='tft-stale'}else if(age>stale){standby('Navigation paused','Waiting for phone data');l.textContent='phone data stale '+age+'ms';l.className='tft-stale'}else{nav(n);l.textContent='UDP live · '+age+'ms';l.className='tft-live'}}
function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(s&&s.developerPreview)paint(s)}).catch(function(){})}setInterval(poll,800);setTimeout(poll,50)})();</script>)SVG");
#endif
  page += F("</main></body></html>");
  return page;
}

String NetworkManager::buildStatusJson() const {
  String json;
  json.reserve(1800);
  json += "{";
  json += "\"connected\":";
  json += isConnected() ? "true" : "false";
  json += ",\"wifiStatus\":\"" + jsonEscape(wifiStatusName()) + "\"";
  json += ",\"ssid\":\"" + jsonEscape(activeSsid) + "\"";
  json += ",\"source\":\"" + jsonEscape(credentialSource) + "\"";
  json += ",\"staIp\":\"" + jsonEscape(isConnected() ? WiFi.localIP().toString() : String("")) + "\"";
  json += ",\"portalActive\":";
  json += isConfigPortalActive() ? "true" : "false";
  json += ",\"portalSsid\":\"" + jsonEscape(portalSsidName) + "\"";
  json += ",\"portalUrl\":\"" + jsonEscape(configPortalUrl()) + "\"";
  json += ",\"udpPort\":";
  json += String(AMAP_UDP_PORT);
  json += ",\"bleConnected\":";
  json += bleReceiver != nullptr && bleReceiver->isConnected() ? "true" : "false";
  json += ",\"bleName\":\"";
  json += jsonEscape(bleReceiver != nullptr ? bleReceiver->deviceName() : String(""));
  json += "\"";
  json += ",\"bleBondCount\":";
  json += String(bleReceiver != nullptr ? bleReceiver->bondCount() : 0);
  json += ",\"lastError\":\"" + jsonEscape(lastError) + "\"";
  json += ",\"developerPreview\":";
  json += developerPreviewEnabled ? "true" : "false";
  json += ",\"tft\":{\"width\":320,\"height\":240,\"staleMs\":" + String(AMAP_STALE_MS);
  json += ",\"standbyMs\":" + String(AMAP_STANDBY_MS) + "}";
  json += ",\"nav\":";
  json += buildNavigationJson();
  if (otaManager) {
    json += ",\"ota\":";
    json += otaManager->statusJson();
  }
  json += "}";
  return json;
}

String NetworkManager::buildNavigationJson() const {
  NavState empty;
  const NavState& state = navigationState ? *navigationState : empty;
  String laneText;
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    if (!laneText.isEmpty()) {
      laneText += " ";
    }
    laneText += state.lane.advised[i] ? "[" : "";
    laneText += String(state.lane.lanes[i]);
    laneText += state.lane.advised[i] ? "]" : "";
  }

  String lightText;
  for (uint8_t i = 0; i < state.lightCount; ++i) {
    const LightState& light = state.lights[i];
    if (!lightText.isEmpty()) {
      lightText += "  ";
    }
    const char* direction = light.dir == 0 ? "掉头" :
                            light.dir == 1 ? "左" :
                            (light.dir == 2 || light.dir == 3) ? "右" :
                            light.dir == 4 ? "直" : "";
    const char* color = light.status == 1 ? "红" : (light.status == 4 ? "绿" : "黄");
    lightText += String(direction) + color + " " + String(light.seconds) + "s";
  }

  String json;
  json.reserve(1600);
  json += "{";
  json += "\"active\":";
  json += state.active ? "true" : "false";
  json += ",\"mode\":\"" + jsonEscape(state.mode) + "\"";
  json += ",\"road\":\"" + jsonEscape(state.road) + "\"";
  json += ",\"heading\":\"" + jsonEscape(state.heading) + "\"";
  json += ",\"turn\":{\"icon\":" + String(state.turn.icon);
  json += ",\"text\":\"" + jsonEscape(state.turn.text) + "\"";
  json += ",\"distanceText\":\"" + jsonEscape(state.turn.distanceText) + "\"";
  json += ",\"road\":\"" + jsonEscape(state.turn.road) + "\"}";
  json += ",\"eta\":{\"remainDistanceText\":\"" +
          jsonEscape(state.eta.remainDistanceText) + "\"";
  json += ",\"remainTimeText\":\"" + jsonEscape(state.eta.remainTimeText) + "\"}";
  json += ",\"speed\":{\"current\":" + String(state.speed.current);
  json += ",\"limit\":" + String(state.speed.limit) + "}";
  json += ",\"camera\":{\"type\":" + String(state.camera.type);
  json += ",\"distance\":" + String(state.camera.distance);
  json += ",\"speedLimit\":" + String(state.camera.speedLimit) + "}";
  json += ",\"lane\":{\"lanes\":[";
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    if (i > 0) json += ",";
    json += String(state.lane.lanes[i]);
  }
  json += "],\"advised\":[";
  for (uint8_t i = 0; i < state.lane.count; ++i) {
    if (i > 0) json += ",";
    json += state.lane.advised[i] ? "true" : "false";
  }
  json += "]}";
  json += ",\"lights\":[";
  for (uint8_t i = 0; i < state.lightCount; ++i) {
    if (i > 0) json += ",";
    json += "{\"dir\":" + String(state.lights[i].dir);
    json += ",\"status\":" + String(state.lights[i].status);
    json += ",\"seconds\":" + String(state.lights[i].seconds) + "}";
  }
  json += "]";
  json += ",\"tmc\":{\"totalDistance\":" + String(state.tmc.totalDistance);
  json += ",\"finishDistance\":" + String(state.tmc.finishDistance);
  json += ",\"segments\":[";
  for (uint8_t i = 0; i < state.tmc.count; ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"status\":" + String(state.tmc.status[i]);
    json += ",\"distance\":" + String(state.tmc.distance[i]) + "}";
  }
  json += "]}";
  json += ",\"route\":{\"progressPercent\":" + String(state.route.progressPercent);
  json += ",\"destination\":\"" + jsonEscape(state.route.destination) + "\"";
  json += ",\"remainingTrafficLights\":" + String(state.route.remainingTrafficLights) + "}";
  json += ",\"roadInfo\":{\"type\":\"" + jsonEscape(state.roadInfo.type) + "\"";
  json += ",\"traffic\":\"" + jsonEscape(state.roadInfo.traffic) + "\"";
  json += ",\"crossMap\":";
  json += state.roadInfo.crossMap ? "true" : "false";
  json += "}";
  json += ",\"guide\":{\"exitName\":\"" + jsonEscape(state.guide.exitName) + "\"";
  json += ",\"exitDirection\":\"" + jsonEscape(state.guide.exitDirection) + "\"";
  json += ",\"serviceAreaName\":\"" + jsonEscape(state.guide.serviceAreaName) + "\"";
  json += ",\"serviceAreaDistance\":\"" + jsonEscape(state.guide.serviceAreaDistance) + "\"}";
  json += ",\"music\":{\"active\":";
  json += state.music.active ? "true" : "false";
  json += ",\"playing\":";
  json += state.music.playing ? "true" : "false";
  json += ",\"source\":\"" + jsonEscape(state.music.source) + "\"";
  json += ",\"songId\":" + String(static_cast<long long>(state.music.songId));
  json += ",\"title\":\"" + jsonEscape(state.music.title) + "\"";
  json += ",\"artist\":\"" + jsonEscape(state.music.artist) + "\"";
  json += ",\"album\":\"" + jsonEscape(state.music.album) + "\"";
  json += ",\"positionMs\":" + String(static_cast<long long>(state.music.positionMs));
  json += ",\"durationMs\":" + String(static_cast<long long>(state.music.durationMs));
  json += ",\"lyric\":\"" + jsonEscape(state.music.lyric) + "\"";
  json += ",\"translatedLyric\":\"" + jsonEscape(state.music.translatedLyric) + "\"";
  json += ",\"nextLyric\":\"" + jsonEscape(state.music.nextLyric) + "\"";
  json += ",\"highlightedLyric\":\"" + jsonEscape(state.music.highlightedLyric) + "\"";
  json += ",\"wordProgressPermille\":" + String(state.music.wordProgressPermille) + "}";
  json += ",\"laneText\":\"" + jsonEscape(laneText) + "\"";
  json += ",\"lightText\":\"" + jsonEscape(lightText) + "\"";
  json += ",\"alert\":\"" + jsonEscape(state.alert) + "\"";
  json += ",\"packetAgeMs\":";
  json += state.lastPacketAt == 0 ? String(-1) : String(millis() - state.lastPacketAt);
  json += "}";
  return json;
}

String NetworkManager::wifiStatusName() const {
  switch (WiFi.status()) {
    case WL_CONNECTED:
      return "已连接";
    case WL_NO_SSID_AVAIL:
      return "找不到热点";
    case WL_CONNECT_FAILED:
      return "连接失败";
    case WL_CONNECTION_LOST:
      return "连接丢失";
    case WL_DISCONNECTED:
      return staConnecting ? "连接中" : "未连接";
    case WL_IDLE_STATUS:
      return "空闲";
    default:
      return "未知状态_" + String(static_cast<int>(WiFi.status()));
  }
}

String NetworkManager::htmlEscape(String value) const {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String NetworkManager::jsonEscape(String value) const {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "\\r");
  value.replace("\n", "\\n");
  return value;
}

String NetworkManager::makePortalSsid() const {
  const uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(mac & 0xFFFFFF));
  return String(AMAP_CONFIG_AP_SSID_PREFIX) + suffix;
}

void NetworkManager::beginUdpIfNeeded() {
  if (udpStarted) {
    return;
  }
  udpStarted = udp.begin(AMAP_UDP_PORT);
}

bool NetworkManager::isPortalRadioActive() const {
  const wifi_mode_t mode = WiFi.getMode();
  return (mode & WIFI_AP) != 0 && !WiFi.softAPSSID().isEmpty();
}

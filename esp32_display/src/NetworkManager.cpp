#include "NetworkManager.h"

#include "Config.h"

#include <Esp.h>

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

void NetworkManager::begin(OtaManager* ota, const NavState* navigation) {
  otaManager = ota;
  navigationState = navigation;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  portalSsidName = makePortalSsid();
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
  webServer.on("/status.json", HTTP_GET, [this]() { handleStatusJson(); });
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
  if (!otaManager) {
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
    if (!otaManager) {
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
  if (!otaManager) {
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
  page.reserve(14500);
  page += F("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>AMap ESP32 配置</title><style>");
  page += F("body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f5f7fb;color:#172033}");
  page += F("main{max-width:680px;margin:0 auto;padding:24px 16px 40px}");
  page += F("h1{font-size:24px;margin:0 0 6px}.sub{color:#5f6b7a;margin:0 0 18px}");
  page += F(".panel{background:#fff;border:1px solid #dce3ee;border-radius:8px;padding:16px;margin:14px 0;box-shadow:0 1px 2px rgba(0,0,0,.04)}");
  page += F(".grid{display:grid;grid-template-columns:130px 1fr;gap:10px 12px}.k{color:#637083}.v{font-weight:600;word-break:break-word}");
  page += F(".ok{color:#087443}.bad{color:#b42318}.msg{background:#e8f1ff;border-color:#b8d3ff}.err{background:#fff1f0;border-color:#ffccc7}");
  page += F("label{display:block;font-weight:650;margin:12px 0 6px}input,select{width:100%;box-sizing:border-box;border:1px solid #c8d2df;border-radius:6px;padding:11px;font-size:16px;background:#fff}");
  page += F("button{border:0;border-radius:6px;background:#1769e0;color:white;font-weight:700;padding:11px 14px;font-size:15px;margin-top:14px;cursor:pointer}");
  page += F("button:disabled{background:#9aa5b1;cursor:not-allowed}button.secondary{background:#687385}.hint{font-size:13px;color:#637083;line-height:1.5}.notes{white-space:pre-wrap;font-weight:500;line-height:1.5}");
  page += F(".dev-title{display:flex;justify-content:space-between;align-items:center;gap:12px}.dev-tag{font:700 11px/1 monospace;letter-spacing:.08em;color:#3c6eaf}.toggle{display:flex;align-items:center;gap:9px;font-weight:650}.toggle input{width:auto;accent-color:#1769e0}.tft{margin-top:16px;background:#0a1017;color:#e8f1ff;border:1px solid #30455d;border-radius:18px;padding:12px;box-shadow:inset 0 0 0 2px #122131,0 12px 28px rgba(8,20,35,.2);font-family:ui-monospace,'Cascadia Code',monospace}.tft-head{display:grid;grid-template-columns:auto 1fr auto;gap:9px;align-items:center;color:#a6c6e7;font-size:11px}.tft-mode{color:#69d6ff;font-weight:800;letter-spacing:.1em}.tft-road{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.tft-speed{font-size:16px;font-weight:800;color:#fff}.tft-turn{display:grid;grid-template-columns:58px 1fr;gap:10px;margin:13px 0 10px;align-items:center}.tft-arrow{font-size:44px;line-height:1;color:#52d1ff;text-align:center}.tft-turn b,.tft-turn small{display:block}.tft-turn b{font-size:18px}.tft-turn small{margin-top:3px;color:#96a9bd}.tft-progress{height:5px;border-radius:99px;background:#24394e;overflow:hidden;margin:9px 0}.tft-progress i{display:block;height:100%;width:0;background:linear-gradient(90deg,#41c6ff,#a5eb65)}.tft-tmc-head{display:flex;justify-content:space-between;margin:11px 0 5px;color:#96a9bd;font-size:10px;letter-spacing:.08em}.tft-tmc{position:relative;height:8px;border-radius:99px;background:#24394e;overflow:visible}.tmc-segments{display:flex;height:100%;border-radius:99px;overflow:hidden}.tmc-segments i{display:block;height:100%;min-width:1px}.tmc-marker{position:absolute;top:-4px;width:0;height:0;border-left:5px solid transparent;border-right:5px solid transparent;border-top:8px solid #f4fbff;filter:drop-shadow(0 1px 1px #000);transform:translateX(-50%)}.tft-meta{display:grid;grid-template-columns:repeat(2,1fr);gap:7px;color:#b7c9d8;font-size:11px;margin-top:10px}.tft-meta span{border-left:2px solid #2d8aac;padding-left:6px;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.tft-alert{margin-top:10px;color:#ffd56a;font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}");
  page += F(".progress{margin-top:4px}.progress-track{height:10px;background:#e3eaf5;border-radius:999px;overflow:hidden}.progress-bar{height:100%;width:0;background:#1769e0;transition:width .2s ease}.progress-meta{display:flex;justify-content:space-between;gap:12px;margin-top:8px;font-size:13px;color:#4d5b6c}</style></head><body><main>");
  page += F("<h1>AMap ESP32 配置</h1><p class=\"sub\">网络状态、配网页面与 OTA 工具。</p>");

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
  page += F("</div><div class=\"k\">配网热点</div><div id=\"portalSsid\" class=\"v\">");
  page += isConfigPortalActive() ? htmlEscape(portalSsidName) : String("未启用");
  page += F("</div><div class=\"k\">配网页面</div><div id=\"portalUrl\" class=\"v\">");
  page += isConfigPortalActive() ? htmlEscape(configPortalUrl())
                                 : htmlEscape(String("http://") + WiFi.localIP().toString() + "/");
  page += F("</div></div></section>");

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

  page += F("<section class=\"panel\"><div class=\"dev-title\"><h2 style=\"font-size:18px;margin:0\">开发者选项</h2><span class=\"dev-tag\">TFT LAB</span></div>");
  page += F("<form method=\"post\" action=\"/developer/preview\"><label class=\"toggle\"><input type=\"checkbox\" name=\"enabled\" value=\"1\"");
  page += developerPreviewEnabled ? F(" checked") : F("");
  page += F(">启用 SPI TFT 模拟显示</label><button class=\"secondary\" type=\"submit\">保存开发者选项</button></form>");
  page += F("<p class=\"hint\">严格使用最近一次成功解析的 UDP JSON；未收到有效帧时显示等待状态。此选项只影响配置页预览，不改变 OLED 输出。</p>");
  page += F("<div id=\"tftPreview\" class=\"tft\"");
  page += developerPreviewEnabled ? F("") : F(" style=\"display:none\"");
  page += F("><div class=\"tft-head\"><span id=\"tftMode\" class=\"tft-mode\">WAIT</span><span id=\"tftRoad\" class=\"tft-road\">等待 UDP JSON</span><span id=\"tftSpeed\" class=\"tft-speed\">--</span></div>");
  page += F("<div class=\"tft-turn\"><span id=\"tftArrow\" class=\"tft-arrow\">·</span><div><b id=\"tftTurn\">尚未接收到有效导航帧</b><small id=\"tftNext\">请从 Android 转发器发送测试帧</small></div></div>");
  page += F("<div class=\"tft-progress\"><i id=\"tftProgress\" style=\"width:0%\"></i></div><div class=\"tft-tmc-head\"><span>TMC TRAFFIC</span><span id=\"tftTmcLabel\">等待数据</span></div><div class=\"tft-tmc\"><div id=\"tftTmcSegments\" class=\"tmc-segments\"></div><i id=\"tftTmcMarker\" class=\"tmc-marker\" style=\"left:0%;display:none\"></i></div><div class=\"tft-meta\"><span id=\"tftEta\">--</span><span id=\"tftDestination\">--</span><span id=\"tftGuide\">--</span><span id=\"tftTraffic\">--</span></div><div id=\"tftAlert\" class=\"tft-alert\">等待 UDP JSON</div></div></section>");

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
  page += F("<label for=\"otaChannelSelect\">更新渠道</label><select id=\"otaChannelSelect\" name=\"channel\" style=\"width:100%;box-sizing:border-box;border:1px solid #c8d2df;border-radius:6px;padding:11px;font-size:16px;background:#fff\">");
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
  page += F("<p class=\"hint\">所选更新渠道可以与当前运行固件的渠道不同。更新为手动触发；manifest 与固件 URL 遇到重定向时会自动跟随。</p></section>");

  page += F("<script>(function(){function e(i){return document.getElementById(i)}function t(i,v){var n=e(i);if(n)n.textContent=v||''}function sc(){var c=e('otaChannelSelect');var h=e('otaUpgradeChannel');if(c&&h)h.value=c.value||'stable'}function bc(){var c=e('otaChannelSelect');if(!c||c.dataset.bound)return;c.dataset.bound='1';c.addEventListener('change',function(){c.dataset.dirty='1';c.dataset.pending=c.value||'stable';sc()});c.addEventListener('input',function(){c.dataset.dirty='1';c.dataset.pending=c.value||'stable';sc()});sc()}");
  page += F("function d(v,f){return v===undefined||v===null||v===''?f:v}function tc(s){return s===10?'#666':s===0?'#2196f3':s===1?'#1abf54':s===2?'#ffd600':s===3?'#ff1744':s===4?'#b71c1c':s===5?'#007d5d':'#333'}function tm(n){var x=(n||{}).tmc||{},a=x.segments||[],b=e('tftTmcSegments'),m=e('tftTmcMarker'),l=e('tftTmcLabel');if(!b)return;b.textContent='';if(!a.length||!(x.totalDistance>0)){if(l)l.textContent='等待数据';if(m)m.style.display='none';return}var sum=0;for(var i=0;i<a.length;i++)sum+=Math.max(0,Number(a[i].distance)||0);if(!(sum>0))sum=x.totalDistance;for(var j=0;j<a.length;j++){var q=document.createElement('i');q.style.flex=String(Math.max(0,Number(a[j].distance)||0));q.style.background=tc(Number(a[j].status));b.appendChild(q)}var p=Math.max(0,Math.min(100,(Number(x.finishDistance)||0)/Number(x.totalDistance)*100));if(m){m.style.left=String(p)+'%';m.style.display='block'}if(l)l.textContent=String(a.length)+' 段 · '+Math.round(p)+'%'}function pv(n){n=n||{};if(!n.active){t('tftMode','WAIT');t('tftRoad','等待 UDP JSON');t('tftSpeed','--');t('tftArrow','·');t('tftTurn','尚未接收到有效导航帧');t('tftNext','请从 Android 转发器发送测试帧');t('tftEta','--');t('tftDestination','--');t('tftGuide','--');t('tftTraffic','--');t('tftAlert','等待 UDP JSON');var z=e('tftProgress');if(z)z.style.width='0%';tm({});return}var r=n.route||{},g=n.guide||{},ri=n.roadInfo||{},u=n.turn||{},s=n.speed||{};var icon=Number(u.icon||0),a=(icon===2||icon===4||icon===6||icon===18)?'←':((icon===8)?'↻':((icon===3||icon===5||icon===7||icon===19)?'→':'↑'));t('tftMode',String(d(n.mode,'nav')).toUpperCase());t('tftRoad',d(n.road,'--'));t('tftSpeed',s.current>=0?s.current:'--');t('tftArrow',a);t('tftTurn',d((d(u.text,'直行'))+' '+d(u.distanceText,''),'直行'));t('tftNext',d(u.road,'--'));t('tftEta',(d((n.eta||{}).remainTimeText,'--'))+' · '+d((n.eta||{}).remainDistanceText,'--'));t('tftDestination',d(r.destination,'--'));t('tftGuide',d(g.exitName?g.exitName+(g.exitDirection?' · '+g.exitDirection:''):(g.serviceAreaName?'服务区 '+g.serviceAreaName+' '+d(g.serviceAreaDistance,''):'--'),'--'));t('tftTraffic',(d(ri.type,'--'))+(ri.traffic?' · '+ri.traffic:'')+(ri.crossMap?' · 路口放大图':''));t('tftAlert',d(n.alert,d(n.lightText,'--')));var p=Number(r.progressPercent);if(!(p>=0&&p<=100))p=0;var b=e('tftProgress');if(b)b.style.width=String(p)+'%';tm(n)}");
  page += F("function poll(){fetch('/status.json',{cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(function(s){if(!s)return;");
  page += F("var w=e('wifiStatus');if(w){w.textContent=s.wifiStatus||'';w.className='v '+(s.connected?'ok':'bad')}");
  page += F("t('currentSsid',s.ssid||'未配置');t('credentialSource',s.source||'none');t('staIp',s.staIp||'未连接');t('udpPort',String(s.udpPort||''));");
  page += F("t('portalSsid',s.portalActive?s.portalSsid:'未启用');t('portalUrl',s.portalActive?s.portalUrl:(s.staIp?('http://'+s.staIp+'/'):'未连接'));");
  page += F("if(s.ota){t('otaCurrent',s.ota.currentVersion+' build '+s.ota.currentBuild);t('otaChannel',s.ota.currentChannel);t('otaSelectedChannel',s.ota.selectedChannel||'');t('otaLatest',s.ota.latestBuildInfo||'未检查');t('otaStatus',s.ota.status);t('otaNotes',s.ota.changelog||s.ota.releaseNotes||'');t('otaError',s.ota.lastError||'');var c=e('otaChannelSelect');if(c&&s.ota.selectedChannel)c.value=s.ota.selectedChannel;var h=document.querySelector('form[action=\"/ota/upgrade\"] input[name=\"channel\"]');if(h&&s.ota.selectedChannel)h.value=s.ota.selectedChannel}");
  page += F("if(s.ota){var pb=e('otaProgressBar');if(pb)pb.style.width=String(s.ota.progressPercent||0)+'%';t('otaProgressText',s.ota.progressText||'未开始');t('otaProgressPercent',String(s.ota.progressPercent||0)+'%');var busy=!!s.ota.busy;var cb=document.querySelector('form[action=\"/ota/check\"] button');if(cb)cb.disabled=busy;var ub=document.querySelector('form[action=\"/ota/upgrade\"] button');if(ub)ub.disabled=busy;}");
  page += F("if(s.developerPreview)pv(s.nav);");
  page += F("var c2=e('otaChannelSelect');if(c2&&c2.dataset.dirty&&c2.dataset.pending){if(s.ota&&c2.dataset.pending===s.ota.selectedChannel){c2.dataset.dirty='';c2.dataset.pending=''}else{c2.value=c2.dataset.pending;sc()}}");
  page += F("var p=e('errorPanel');var l=e('lastError');if(p&&l){l.textContent=s.lastError||'';p.style.display=s.lastError?'':'none'}}).catch(function(){})}");
  page += F("bc();setInterval(poll,800);setTimeout(poll,200)})();</script>");
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
  json += ",\"lastError\":\"" + jsonEscape(lastError) + "\"";
  json += ",\"developerPreview\":";
  json += developerPreviewEnabled ? "true" : "false";
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
  if (state.lightCount > 0) {
    const LightState& light = state.lights[0];
    const char* color = light.status == 1 ? "红灯" : (light.status == 4 ? "绿灯" : "黄灯");
    lightText = String(color) + " " + String(light.seconds) + "s";
  }

  String json;
  json.reserve(1050);
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
  json += ",\"laneText\":\"" + jsonEscape(laneText) + "\"";
  json += ",\"lightText\":\"" + jsonEscape(lightText) + "\"";
  json += ",\"alert\":\"" + jsonEscape(state.alert) + "\"";
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

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

void NetworkManager::handleTftBitmap() {
  if (!developerPreviewEnabled) {
    webServer.send(404, "text/plain; charset=utf-8", "TFT preview is disabled");
    return;
  }

  NavState empty;
  const NavState& state = navigationState ? *navigationState : empty;
  const unsigned long silenceMs = state.lastPacketAt == 0 ? ULONG_MAX : millis() - state.lastPacketAt;
  if (!tftPreview.sendBmp(webServer, state, isConnected(), silenceMs)) {
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
  page.reserve(26000);
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
  page += F(".dev-title{display:flex;justify-content:space-between;align-items:center;gap:12px}.dev-tag{font:700 11px/1 monospace;letter-spacing:.08em;color:#3c6eaf}.toggle{display:flex;align-items:center;gap:9px;font-weight:650}.toggle input{width:auto;accent-color:#1769e0}.tft-shell{margin-top:16px;padding:18px;border-radius:26px;background:#14171b;box-shadow:inset 0 0 0 2px #343a41,inset 0 0 0 7px #080a0c,0 18px 38px rgba(5,12,20,.28)}.tft-glass{position:relative;overflow:hidden;width:100%;aspect-ratio:4/3;background:#000;border-radius:4px;box-shadow:0 0 0 1px #000,0 0 24px rgba(75,220,255,.08)}.tft-glass:after{content:'';pointer-events:none;position:absolute;inset:0;background:linear-gradient(115deg,rgba(255,255,255,.035),transparent 28%,transparent 72%,rgba(255,255,255,.018));mix-blend-mode:screen}.tft-canvas{display:block;width:100%;height:100%;image-rendering:pixelated}.tft-caption{display:flex;justify-content:space-between;gap:12px;margin-top:11px;color:#657184;font:700 11px/1.4 ui-monospace,'Cascadia Code',monospace}.tft-caption span:last-child{text-align:right}.tft-live{color:#087443}.tft-stale{color:#b45309}");
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
  page += F("<p class=\"hint\">硬件数字孪生严格使用 ST7789 320×240 横屏的实际坐标、RGB565 色板和渲染优先级；显示内容来自最近一次成功解析的 UDP JSON，不改变 OLED 输出。</p>");
  page += F("<div id=\"tftPreview\" class=\"tft-shell\"");
  page += developerPreviewEnabled ? F("") : F(" style=\"display:none\"");
  page += F("><div class=\"tft-glass\"><img id=\"tftFrame\" class=\"tft-canvas\" src=\"/tft.bmp\" alt=\"ST7789 320x240 hardware frame\"></div><div class=\"tft-caption\"><span>ST7789 · 320×240 · ROTATION 1</span><span id=\"tftFreshness\" class=\"tft-stale\">等待 UDP</span></div></div></section>");

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
  page += F("if(s.developerPreview){var tf=e('tftFreshness'),n=s.nav||{},age=Number(n.packetAgeMs),cfg=s.tft||{},stale=Number(cfg.staleMs)||3000,wait=Number(cfg.standbyMs)||10000;if(tf){if(!s.connected){tf.textContent='Wi-Fi disconnected';tf.className='tft-stale'}else if(!n.active||age<0||age>wait){tf.textContent='waiting for navigation';tf.className='tft-stale'}else if(age>stale){tf.textContent='phone data stale '+age+'ms';tf.className='tft-stale'}else{tf.textContent='UDP live · '+age+'ms';tf.className='tft-live'}}}");
  page += F("var c2=e('otaChannelSelect');if(c2&&c2.dataset.dirty&&c2.dataset.pending){if(s.ota&&c2.dataset.pending===s.ota.selectedChannel){c2.dataset.dirty='';c2.dataset.pending=''}else{c2.value=c2.dataset.pending;sc()}}");
  page += F("var p=e('errorPanel');var l=e('lastError');if(p&&l){l.textContent=s.lastError||'';p.style.display=s.lastError?'':'none'}}).catch(function(){})}");
  page += F("bc();setInterval(poll,800);setTimeout(poll,200)})();</script>");
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

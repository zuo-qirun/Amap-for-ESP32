#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "NavState.h"
#include "OtaManager.h"
#include "TftPreviewRenderer.h"

class NetworkManager {
public:
  NetworkManager();
  void begin(OtaManager* ota, const NavState* navigation = nullptr);
  void update();
  int readPacket(char* buffer, size_t capacity, IPAddress& remoteIp, uint16_t& remotePort);
  bool isConnected() const;
  bool isConfigPortalActive() const;
  bool isWebReady() const;
  String ipString() const;
  String configPortalSsid() const;
  String configPortalUrl() const;
  String statusText() const;

private:
  WiFiUDP udp;
  DNSServer dnsServer;
  WebServer webServer;
  IPAddress portalIp;
  IPAddress portalGateway;
  IPAddress portalSubnet;
  OtaManager* otaManager = nullptr;
  const NavState* navigationState = nullptr;
  TftPreviewRenderer tftPreview;
  String activeSsid;
  String activePassword;
  String credentialSource;
  String portalSsidName;
  String lastError;
  String portalMessage;
  bool webServerStarted = false;
  bool routesConfigured = false;
  bool portalActive = false;
  bool staConnecting = false;
  bool reconnectScheduled = false;
  bool udpStarted = false;
  bool developerPreviewEnabled = false;
  unsigned long lastReconnectAttempt = 0;
  unsigned long connectStartedAt = 0;
  unsigned long reconnectAt = 0;

  void loadCredentials();
  void loadDeveloperOptions();
  void saveDeveloperPreview(bool enabled);
  void saveCredentials(const String& ssid, const String& password);
  void clearSavedCredentials();
  bool hasFallbackCredentials() const;
  void scheduleStaConnect(unsigned long delayMs);
  void startStaConnect(bool force);
  void startConfigPortal(const String& reason);
  void stopConfigPortal();
  void startWebServerIfNeeded();
  void configureRoutes();
  void handleRoot();
  void handleSave();
  void handleClear();
  void handleOtaCheck();
  void handleOtaUpgrade();
  void handleDeveloperPreview();
  void handleTftBitmap();
  bool applyOtaChannelSelection(String& message);
  void handleStatusJson();
  void handleNotFound();
  void redirectToRoot();
  void redirectToPortal();
  bool shouldRedirectToPortal();
  String buildStatusPage(const String& message = String()) const;
  String buildStatusJson() const;
  String buildNavigationJson() const;
  String wifiStatusName() const;
  String htmlEscape(String value) const;
  String jsonEscape(String value) const;
  String makePortalSsid() const;
  void beginUdpIfNeeded();
  bool isPortalRadioActive() const;
};

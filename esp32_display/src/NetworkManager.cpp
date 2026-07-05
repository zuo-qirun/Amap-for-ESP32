#include "NetworkManager.h"

#include "Config.h"

void NetworkManager::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(AMAP_WIFI_SSID, AMAP_WIFI_PASSWORD);
  lastReconnectAttempt = millis();
}

void NetworkManager::update() {
  if (WiFi.status() == WL_CONNECTED) {
    beginUdpIfNeeded();
    return;
  }
  udpStarted = false;
  unsigned long now = millis();
  if (now - lastReconnectAttempt > 10000UL) {
    lastReconnectAttempt = now;
    WiFi.disconnect();
    WiFi.begin(AMAP_WIFI_SSID, AMAP_WIFI_PASSWORD);
  }
}

int NetworkManager::readPacket(char* buffer, size_t capacity, IPAddress& remoteIp, uint16_t& remotePort) {
  if (!udpStarted || capacity == 0) {
    return 0;
  }
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return 0;
  }
  size_t maxLen = capacity - 1;
  int length = udp.read(buffer, min(static_cast<int>(maxLen), packetSize));
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

String NetworkManager::ipString() const {
  return isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

String NetworkManager::statusText() const {
  if (isConnected()) {
    return "WiFi " + ipString();
  }
  return "WiFi connecting";
}

void NetworkManager::beginUdpIfNeeded() {
  if (udpStarted) {
    return;
  }
  udpStarted = udp.begin(AMAP_UDP_PORT);
}

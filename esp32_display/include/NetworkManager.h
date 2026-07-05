#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

class NetworkManager {
public:
  void begin();
  void update();
  int readPacket(char* buffer, size_t capacity, IPAddress& remoteIp, uint16_t& remotePort);
  bool isConnected() const;
  String ipString() const;
  String statusText() const;

private:
  WiFiUDP udp;
  bool udpStarted = false;
  unsigned long lastReconnectAttempt = 0;
  void beginUdpIfNeeded();
};

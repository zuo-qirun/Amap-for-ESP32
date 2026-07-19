#include <Arduino.h>
#include "Config.h"
#include "BleReceiver.h"
#include "CapacitiveTouch.h"
#include "TftRenderer.h"
#include "NavState.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "ProtocolParser.h"

NetworkManager network;
BleReceiver ble;
OtaManager ota;
ProtocolParser parser;
TftRenderer display;
CapacitiveTouch touch;
NavState navState;

char packetBuffer[AMAP_PACKET_BUFFER_SIZE];
unsigned long lastRenderAt = 0;
unsigned long lastStatusLogAt = 0;
String lastStatusText;
bool displayReady = false;
uint8_t lastTouchCount = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("AMap ESP32-S3 Navigation Display");

  navState.reset();
  display.begin();
  displayReady = display.isReady();
  touch.begin();
  // Initialise the Bluetooth controller before Wi-Fi. On ESP32-S3 the radio
  // coexistence layer must be established by NimBLE before Wi-Fi enters
  // AP/STA mode, otherwise some Arduino core builds abort in coex_enable().
  ble.begin();
  ota.begin();
  network.begin(&ota, &navState, &ble);
}

void loop() {
  network.update();
  touch.update();
  const uint8_t touchCount = touch.touchCount();
  if (touchCount != lastTouchCount) {
    lastTouchCount = touchCount;
    if (touchCount > 0) {
      const CapacitiveTouchPoint point = touch.point();
      Serial.printf("touch down: count=%u x=%d y=%d\n", touchCount, point.x, point.y);
    } else {
      Serial.println("touch up");
    }
  }
  // Do not start a background manifest request while the same flash update
  // partition is being written by a browser upload.
  if (!network.isManualFirmwareUpdatePending()) {
    ota.update(network.isConnected(), network.isWebReady(), displayReady);
  }

  IPAddress remoteIp;
  uint16_t remotePort = 0;
  int length = network.readPacket(packetBuffer, sizeof(packetBuffer), remoteIp, remotePort);
  if (length > 0) {
    String error;
    if (parser.parse(packetBuffer, static_cast<size_t>(length), navState, error)) {
      Serial.printf("UDP %s:%u length=%d seq=%lu mode=%s active=%s lightCount=%u\n",
                    remoteIp.toString().c_str(),
                    remotePort,
                    length,
                    static_cast<unsigned long>(navState.seq),
                    navState.mode.c_str(),
                    navState.active ? "true" : "false",
                    navState.lightCount);
      for (uint8_t i = 0; i < navState.lightCount; ++i) {
        const LightState& light = navState.lights[i];
        Serial.printf("  light[%u] dir=%d status=%d seconds=%d\n",
                      i, light.dir, light.status, light.seconds);
      }
    } else {
      Serial.printf("JSON parse failed: %s\n", error.c_str());
    }
  }

  length = ble.readPacket(packetBuffer, sizeof(packetBuffer));
  if (length > 0) {
    String error;
    if (parser.parse(packetBuffer, static_cast<size_t>(length), navState, error)) {
      Serial.printf("BLE length=%d seq=%lu mode=%s active=%s lightCount=%u\n",
                    length,
                    static_cast<unsigned long>(navState.seq),
                    navState.mode.c_str(),
                    navState.active ? "true" : "false",
                    navState.lightCount);
    } else {
      Serial.printf("BLE JSON parse failed: %s\n", error.c_str());
    }
  }

  unsigned long now = millis();
  if (now - lastRenderAt >= 33UL) {
    lastRenderAt = now;
    unsigned long silenceMs = navState.lastPacketAt == 0 ? ULONG_MAX : now - navState.lastPacketAt;
    display.render(navState, network.isConnected(), ble.isConnected(), network.ipString(),
                   AMAP_UDP_PORT, silenceMs);
  }

  String statusText = network.statusText();
  if (statusText != lastStatusText || now - lastStatusLogAt >= 30000UL) {
    lastStatusLogAt = now;
    lastStatusText = statusText;
    Serial.printf("status=%s udp=%u ble=%s lastPacket=%lu ms\n",
                  statusText.c_str(),
                  AMAP_UDP_PORT,
                  ble.isConnected() ? "connected" : "advertising",
                  navState.lastPacketAt == 0 ? 0UL : now - navState.lastPacketAt);
  }
}

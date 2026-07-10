#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "DisplayRenderer.h"
#include "NavState.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "ProtocolParser.h"

NetworkManager network;
OtaManager ota;
ProtocolParser parser;
DisplayRenderer display;
NavState navState;

char packetBuffer[AMAP_PACKET_BUFFER_SIZE];
unsigned long lastRenderAt = 0;
unsigned long lastStatusLogAt = 0;
bool oledReady = false;

void scanI2CBus() {
  Serial.printf("I2C scan on SDA=%u SCL=%u, OLED addr8=0x%02X addr7=0x%02X\n",
                AMAP_OLED_SDA_PIN,
                AMAP_OLED_SCL_PIN,
                AMAP_OLED_I2C_ADDRESS,
                AMAP_OLED_I2C_ADDRESS >> 1);
  bool found = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      found = true;
      Serial.printf("I2C device found: addr7=0x%02X addr8=0x%02X\n", address, address << 1);
    }
  }
  if (!found) {
    Serial.println("I2C scan found no devices. Check VCC/GND/SDA/SCL and OLED power.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("AMap ESP32-S3 Navigation Display");

  navState.reset();
  display.begin();
  oledReady = true;
  scanI2CBus();
  ota.begin();
  network.begin(&ota, &navState);
}

void loop() {
  network.update();
  ota.update(network.isConnected(), network.isWebReady(), oledReady);

  IPAddress remoteIp;
  uint16_t remotePort = 0;
  int length = network.readPacket(packetBuffer, sizeof(packetBuffer), remoteIp, remotePort);
  if (length > 0) {
    String error;
    if (parser.parse(packetBuffer, static_cast<size_t>(length), navState, error)) {
      Serial.printf("UDP %s:%u seq=%lu mode=%s road=%s turn=%s dist=%s\n",
                    remoteIp.toString().c_str(),
                    remotePort,
                    static_cast<unsigned long>(navState.seq),
                    navState.mode.c_str(),
                    navState.road.c_str(),
                    navState.turn.text.c_str(),
                    navState.turn.distanceText.c_str());
    } else {
      Serial.printf("JSON parse failed: %s\n", error.c_str());
    }
  }

  unsigned long now = millis();
  if (now - lastRenderAt >= 150UL) {
    lastRenderAt = now;
    unsigned long silenceMs = navState.lastPacketAt == 0 ? ULONG_MAX : now - navState.lastPacketAt;
    display.render(navState, network.isConnected(), network.ipString(), AMAP_UDP_PORT, silenceMs);
  }

  if (now - lastStatusLogAt >= 2000UL) {
    lastStatusLogAt = now;
    Serial.printf("status=%s udp=%u lastPacket=%lu ms\n",
                  network.statusText().c_str(),
                  AMAP_UDP_PORT,
                  navState.lastPacketAt == 0 ? 0UL : now - navState.lastPacketAt);
  }
}

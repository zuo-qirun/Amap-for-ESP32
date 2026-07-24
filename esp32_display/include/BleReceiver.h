#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Config.h"

class BleReceiver {
 public:
  BleReceiver();
  ~BleReceiver();

  void begin();
  int readPacket(char* buffer, size_t capacity);
  bool sendMediaControl(const char* action);
  bool isConnected() const;
  String deviceName() const;
  int bondCount() const;
  int clearBondedDevices();

 private:
  class ServerCallbacks;
  class RxCallbacks;
  friend class ServerCallbacks;
  friend class RxCallbacks;

  struct CompletedPacket {
    uint16_t length;
    char data[AMAP_PACKET_BUFFER_SIZE];
  };

  void handleConnect();
  void handleDisconnect();
  void handleWrite(const uint8_t* data, size_t length);
  void resetFrame();

  QueueHandle_t completedQueue;
  NimBLEServer* server;
  NimBLECharacteristic* rxCharacteristic;
  NimBLECharacteristic* txCharacteristic;
  ServerCallbacks* serverCallbacks;
  RxCallbacks* rxCallbacks;
  String advertisedName;
  volatile bool connected;
  bool receiving;
  uint16_t frameId;
  uint16_t totalLength;
  uint16_t receivedLength;
  char reassembly[AMAP_PACKET_BUFFER_SIZE];
  CompletedPacket completedPacket;
  CompletedPacket dequeuedPacket;
};

#include "BleReceiver.h"

#include <cstring>

namespace {
constexpr uint8_t kMagic0 = 0x41;  // A
constexpr uint8_t kMagic1 = 0x4D;  // M
constexpr uint8_t kVersion = 1;
constexpr uint8_t kFlagStart = 0x01;
constexpr uint8_t kFlagEnd = 0x02;
constexpr size_t kHeaderSize = 10;

uint16_t readLe16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}
}  // namespace

class BleReceiver::ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleReceiver* owner) : owner(owner) {}

  void onConnect(NimBLEServer*) override {
    owner->handleConnect();
  }

  void onDisconnect(NimBLEServer*) override {
    owner->handleDisconnect();
  }

 private:
  BleReceiver* owner;
};

class BleReceiver::RxCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit RxCallbacks(BleReceiver* owner) : owner(owner) {}

  void onWrite(NimBLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    owner->handleWrite(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

 private:
  BleReceiver* owner;
};

BleReceiver::BleReceiver()
    : completedQueue(nullptr),
      server(nullptr),
      rxCharacteristic(nullptr),
      serverCallbacks(nullptr),
      rxCallbacks(nullptr),
      connected(false),
      receiving(false),
      frameId(0),
      totalLength(0),
      receivedLength(0),
      completedPacket{},
      dequeuedPacket{} {}

BleReceiver::~BleReceiver() {
  if (completedQueue != nullptr) {
    vQueueDelete(completedQueue);
  }
  delete serverCallbacks;
  delete rxCallbacks;
}

void BleReceiver::begin() {
  if (server != nullptr) {
    return;
  }
  completedQueue = xQueueCreate(1, sizeof(CompletedPacket));
  if (completedQueue == nullptr) {
    Serial.println("BLE queue allocation failed");
    return;
  }

  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX",
           static_cast<unsigned long long>(ESP.getEfuseMac() & 0xFFFFFFULL));
  advertisedName = String(AMAP_BLE_DEVICE_NAME_PREFIX) + suffix;
  NimBLEDevice::init(advertisedName.c_str());
  NimBLEDevice::setMTU(AMAP_BLE_MTU);

  server = NimBLEDevice::createServer();
  serverCallbacks = new ServerCallbacks(this);
  server->setCallbacks(serverCallbacks);

  NimBLEService* service = server->createService(AMAP_BLE_SERVICE_UUID);
  rxCharacteristic = service->createCharacteristic(
      AMAP_BLE_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR,
      512);
  rxCallbacks = new RxCallbacks(this);
  rxCharacteristic->setCallbacks(rxCallbacks);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(AMAP_BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  Serial.printf("BLE ready name=%s service=%s\n",
                advertisedName.c_str(), AMAP_BLE_SERVICE_UUID);
}

int BleReceiver::readPacket(char* buffer, size_t capacity) {
  if (completedQueue == nullptr || buffer == nullptr || capacity == 0) {
    return 0;
  }
  if (xQueueReceive(completedQueue, &dequeuedPacket, 0) != pdTRUE) {
    return 0;
  }
  const size_t length = min(static_cast<size_t>(dequeuedPacket.length), capacity);
  memcpy(buffer, dequeuedPacket.data, length);
  return static_cast<int>(length);
}

bool BleReceiver::isConnected() const {
  return connected;
}

String BleReceiver::deviceName() const {
  return advertisedName;
}

void BleReceiver::handleConnect() {
  connected = true;
  resetFrame();
  Serial.println("BLE client connected");
}

void BleReceiver::handleDisconnect() {
  connected = false;
  resetFrame();
  Serial.println("BLE client disconnected; advertising resumed");
  NimBLEDevice::startAdvertising();
}

void BleReceiver::handleWrite(const uint8_t* data, size_t length) {
  if (data == nullptr || length < kHeaderSize || data[0] != kMagic0 ||
      data[1] != kMagic1 || data[2] != kVersion) {
    resetFrame();
    return;
  }

  const uint8_t flags = data[3];
  const uint16_t incomingFrameId = readLe16(data + 4);
  const uint16_t offset = readLe16(data + 6);
  const uint16_t incomingTotalLength = readLe16(data + 8);
  const size_t chunkLength = length - kHeaderSize;

  if ((flags & kFlagStart) != 0) {
    resetFrame();
    if (offset != 0 || incomingTotalLength == 0 ||
        incomingTotalLength > sizeof(reassembly)) {
      return;
    }
    receiving = true;
    frameId = incomingFrameId;
    totalLength = incomingTotalLength;
  }

  if (!receiving || incomingFrameId != frameId ||
      incomingTotalLength != totalLength || offset != receivedLength ||
      chunkLength > static_cast<size_t>(totalLength - receivedLength)) {
    resetFrame();
    return;
  }

  memcpy(reassembly + receivedLength, data + kHeaderSize, chunkLength);
  receivedLength += static_cast<uint16_t>(chunkLength);
  if ((flags & kFlagEnd) == 0) {
    return;
  }
  if (receivedLength != totalLength || completedQueue == nullptr) {
    resetFrame();
    return;
  }

  completedPacket.length = totalLength;
  memcpy(completedPacket.data, reassembly, totalLength);
  xQueueOverwrite(completedQueue, &completedPacket);
  Serial.printf("BLE frame complete id=%u length=%u\n", frameId, totalLength);
  resetFrame();
}

void BleReceiver::resetFrame() {
  receiving = false;
  frameId = 0;
  totalLength = 0;
  receivedLength = 0;
}

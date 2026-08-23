#include "fridge_ble.h"
#include "NimBLEDevice.h"
#include <vector>

FridgeBLE Fridge;

static const char *NAME_PREFIXES[] = {"A1-", "AK1-", "AK2-", "AK3-"};
static const int NUM_PREFIXES = 4;

// Fill in once known, or leave blank to auto-connect to the first name match.
static const char *FRIDGE_BLE_ADDRESS = "";

static NimBLEUUID SERVICE_UUID("00001234-0000-1000-8000-00805f9b34fb");
static NimBLEUUID WRITE_UUID("00001235-0000-1000-8000-00805f9b34fb");
static NimBLEUUID NOTIFY_UUID("00001236-0000-1000-8000-00805f9b34fb");

#pragma pack(push, 1)
struct StatusReport {
  uint16_t preamble;
  uint8_t dataLen;
  uint8_t commandCode;
  FridgeSettings settings;
  FridgeSensors sensors;
  uint16_t checksum;
};
#pragma pack(pop)

static FridgeSettings s_settings = {};
static FridgeSensors s_sensors = {};
static bool s_haveStatus = false;

static NimBLEClient *pClient = nullptr;
static NimBLERemoteCharacteristic *pWriteChar = nullptr;
static NimBLERemoteCharacteristic *pNotifyChar = nullptr;
static NimBLEAdvertisedDevice *targetDevice = nullptr;
static volatile bool doConnect = false;
static volatile bool connected = false;

static FridgeStatusCallback s_onStatus;
static FridgeConnCallback s_onConn;

bool FridgeBLE::haveStatus() const { return s_haveStatus; }
const FridgeSettings &FridgeBLE::settings() const { return s_settings; }
const FridgeSensors &FridgeBLE::sensors() const { return s_sensors; }

static void appendChecksum(std::vector<uint8_t> &frame) {
  uint32_t sum = 0;
  for (uint8_t b : frame) sum += b;
  uint16_t cksum = sum & 0xFFFF;
  frame.push_back((cksum >> 8) & 0xFF);
  frame.push_back(cksum & 0xFF);
}

static std::vector<uint8_t> buildPing() { return {0xfe, 0xfe, 0x03, 0x01, 0x02, 0x00}; }

static std::vector<uint8_t> buildSetTemp(int8_t temp) {
  std::vector<uint8_t> f = {0xfe, 0xfe, 0x04, 0x05, (uint8_t)temp};
  appendChecksum(f);
  return f;
}

static std::vector<uint8_t> buildSetState(const FridgeSettings &s) {
  std::vector<uint8_t> f = {0xfe, 0xfe, 0x11, 0x02};
  f.push_back(s.locked);
  f.push_back(s.on);
  f.push_back(s.ecoMode);
  f.push_back((uint8_t)s.hLvl);
  f.push_back((uint8_t)s.tempSet);
  f.push_back((uint8_t)s.e2);
  f.push_back((uint8_t)s.e1);
  f.push_back((uint8_t)s.hysteresisE3);
  f.push_back((uint8_t)s.softStartE4);
  f.push_back(s.celsiusFahrenheitE5);
  f.push_back((uint8_t)s.e6);
  f.push_back((uint8_t)s.e7);
  f.push_back((uint8_t)s.e8);
  f.push_back((uint8_t)s.e9);
  appendChecksum(f);
  return f;
}

static void writeFrame(const std::vector<uint8_t> &frame) {
  if (pWriteChar == nullptr) return;
  // Confirmed on real hardware: 1235 only advertises Write-Without-Response.
  pWriteChar->writeValue((uint8_t *)frame.data(), frame.size(), false);
}

void FridgeBLE::writeSettings(const FridgeSettings &s) { writeFrame(buildSetState(s)); }

void FridgeBLE::setPower(bool on) {
  if (!s_haveStatus) return;
  FridgeSettings s = s_settings;
  s.on = on ? 1 : 0;
  writeSettings(s);
}

void FridgeBLE::setEcoMode(bool eco) {
  if (!s_haveStatus) return;
  FridgeSettings s = s_settings;
  s.ecoMode = eco ? 1 : 0;
  writeSettings(s);
}

void FridgeBLE::setLock(bool locked) {
  if (!s_haveStatus) return;
  FridgeSettings s = s_settings;
  s.locked = locked ? 1 : 0;
  writeSettings(s);
}

void FridgeBLE::setBatteryProtection(int8_t level) {
  if (!s_haveStatus) return;
  FridgeSettings s = s_settings;
  s.hLvl = level;
  writeSettings(s);
}

void FridgeBLE::setTemperature(int8_t temp) { writeFrame(buildSetTemp(temp)); }

static void notifyCallback(NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
  if (length < sizeof(StatusReport)) return; // short echo/ack frame, ignore
  StatusReport r;
  memcpy(&r, data, sizeof(r));
  s_settings = r.settings;
  s_sensors = r.sensors;
  s_haveStatus = true;
  if (s_onStatus) s_onStatus(s_settings, s_sensors);
}

class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override {
    connected = true;
    if (s_onConn) s_onConn(FridgeConnState::Connected);
  }
  void onDisconnect(NimBLEClient *) override {
    connected = false;
    s_haveStatus = false;
    if (s_onConn) s_onConn(FridgeConnState::Disconnected);
  }
};

static bool nameMatches(const std::string &name) {
  for (int i = 0; i < NUM_PREFIXES; i++)
    if (name.rfind(NAME_PREFIXES[i], 0) == 0) return true;
  return false;
}

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    bool addressMatch = strlen(FRIDGE_BLE_ADDRESS) > 0 &&
                         dev->getAddress().toString() == FRIDGE_BLE_ADDRESS;
    bool nameMatch = dev->haveName() && nameMatches(dev->getName());
    if (addressMatch || (strlen(FRIDGE_BLE_ADDRESS) == 0 && nameMatch)) {
      NimBLEDevice::getScan()->stop();
      targetDevice = new NimBLEAdvertisedDevice(*dev);
      doConnect = true;
    }
  }
};

static bool connectToFridge() {
  if (s_onConn) s_onConn(FridgeConnState::Connecting);
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCB(), false);

  if (!pClient->connect(targetDevice)) return false;

  NimBLERemoteService *svc = pClient->getService(SERVICE_UUID);
  if (svc == nullptr) { pClient->disconnect(); return false; }

  pWriteChar = svc->getCharacteristic(WRITE_UUID);
  pNotifyChar = svc->getCharacteristic(NOTIFY_UUID);
  if (pWriteChar == nullptr || pNotifyChar == nullptr) { pClient->disconnect(); return false; }

  if (pNotifyChar->canNotify()) pNotifyChar->subscribe(true, notifyCallback);
  return true;
}

void FridgeBLE::begin(FridgeStatusCallback onStatus, FridgeConnCallback onConn) {
  s_onStatus = onStatus;
  s_onConn = onConn;

  NimBLEDevice::init("esp32-fridge-panel");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCB());
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(100);
  if (s_onConn) s_onConn(FridgeConnState::Scanning);
  scan->start(10, false);
}

void FridgeBLE::loop() {
  static uint32_t lastPing = 0;
  static uint32_t lastRescan = 0;

  if (doConnect) {
    doConnect = false;
    if (!connectToFridge() && s_onConn) s_onConn(FridgeConnState::Disconnected);
  }

  if (!connected && targetDevice == nullptr && millis() - lastRescan > 8000) {
    lastRescan = millis();
    if (s_onConn) s_onConn(FridgeConnState::Scanning);
    NimBLEDevice::getScan()->start(5, false);
  }

  if (connected && millis() - lastPing > 1500) {
    lastPing = millis();
    writeFrame(buildPing());
  }
}

/*
  Setpower / Vevor / Alpicool car fridge BLE control for ESP32
  ---------------------------------------------------------------
  Protocol reference: see Fridge_BLE_Protocol_Reference.md

  Library required: "NimBLE-Arduino" (install via Arduino Library Manager,
  or PlatformIO: h2zero/NimBLE-Arduino)

  Fill in FRIDGE_BLE_ADDRESS below with your fridge's BLE MAC address.
  Find it by watching the Serial Monitor on first boot: this sketch scans
  for any device whose advertised name starts with "A1-", "AK1-", "AK2-",
  or "AK3-" (the known name prefixes for this OEM board) and prints its
  address - copy that into FRIDGE_BLE_ADDRESS and re-flash, or just leave
  autodetection on (default) and it will connect to the first match it sees.

  Serial commands (type into Serial Monitor, 115200 baud, "Newline" ending):
    on          -> turn fridge on
    off         -> turn fridge off
    eco on      -> enable eco mode
    eco off     -> disable eco mode
    lock on     -> lock keypad
    lock off    -> unlock keypad
    temp <n>    -> set target temperature to n (signed integer, current unit)
    status      -> print last known status
*/

#include "NimBLEDevice.h"

// ---- fill this in once you know it, or leave blank to auto-connect to the first match ----
static const char *FRIDGE_BLE_ADDRESS = ""; // e.g. "ff:22:12:07:01:b4" (lowercase)

static const char *NAME_PREFIXES[] = {"A1-", "AK1-", "AK2-", "AK3-"};
static const int NUM_PREFIXES = 4;

static NimBLEUUID SERVICE_UUID("00001234-0000-1000-8000-00805f9b34fb");
static NimBLEUUID WRITE_UUID("00001235-0000-1000-8000-00805f9b34fb");
static NimBLEUUID NOTIFY_UUID("00001236-0000-1000-8000-00805f9b34fb");

static const uint16_t PREAMBLE = 0xfefe;

#pragma pack(push, 1)
struct Settings {
  uint8_t locked;
  uint8_t on;
  uint8_t ecoMode;
  int8_t hLvl;
  int8_t tempSet;
  int8_t e2; // HighestTempSettingMenuE2 (wire position 5)
  int8_t e1; // LowestTempSettingMenuE1  (wire position 6)
  int8_t hysteresisE3;
  int8_t softStartE4;
  uint8_t celsiusFahrenheitE5; // 1 = Fahrenheit, 0 = Celsius
  int8_t e6, e7, e8, e9;
};

struct Sensors {
  int8_t temp;
  int8_t ub17; // undocumented
  int8_t inputV1;
  int8_t inputV2;
};

struct StatusReport {
  uint16_t preamble;
  uint8_t dataLen;
  uint8_t commandCode;
  Settings settings;
  Sensors sensors;
  uint16_t checksum; // as received, big-endian order in the actual bytes
};
#pragma pack(pop)

static Settings lastSettings = {};
static Sensors lastSensors = {};
static bool haveStatus = false;

static NimBLEClient *pClient = nullptr;
static NimBLERemoteCharacteristic *pWriteChar = nullptr;
static NimBLERemoteCharacteristic *pNotifyChar = nullptr;
static NimBLEAdvertisedDevice *targetDevice = nullptr;
static bool doConnect = false;
static bool connected = false;

// ---- checksum: plain sum of all preceding bytes, big-endian output ----
static void appendChecksum(std::vector<uint8_t> &frame) {
  uint32_t sum = 0;
  for (uint8_t b : frame) sum += b;
  uint16_t cksum = sum & 0xFFFF;
  frame.push_back((cksum >> 8) & 0xFF); // hi byte first (big-endian)
  frame.push_back(cksum & 0xFF);        // lo byte
}

static std::vector<uint8_t> buildPing() {
  return {0xfe, 0xfe, 0x03, 0x01, 0x02, 0x00}; // static, pre-verified
}

static std::vector<uint8_t> buildSetTemp(int8_t temp) {
  std::vector<uint8_t> f = {0xfe, 0xfe, 0x04, 0x05, (uint8_t)temp};
  appendChecksum(f);
  return f;
}

// Echoes back lastSettings with one or more fields overridden by the caller
// before calling this - see setPower()/setEcoMode()/setLock()/setTemperature().
static std::vector<uint8_t> buildSetState(const Settings &s) {
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
  Serial.print("TX: ");
  for (uint8_t b : frame) { if (b < 0x10) Serial.print('0'); Serial.print(b, HEX); Serial.print(' '); }
  Serial.println();
  // IMPORTANT: characteristic 1235 on real hardware only advertises
  // Write-Without-Response (confirmed via nRF Connect capture) - requesting an
  // acknowledged write (response=true) can silently fail. Always write with
  // response=false here.
  pWriteChar->writeValue((uint8_t *)frame.data(), frame.size(), false);
}

// ---- public control functions ----
void setPower(bool on) {
  if (!haveStatus) { Serial.println("No status yet, wait for first notification."); return; }
  Settings s = lastSettings;
  s.on = on ? 1 : 0;
  writeFrame(buildSetState(s));
}

void setEcoMode(bool eco) {
  if (!haveStatus) { Serial.println("No status yet, wait for first notification."); return; }
  Settings s = lastSettings;
  s.ecoMode = eco ? 1 : 0;
  writeFrame(buildSetState(s));
}

void setLock(bool locked) {
  if (!haveStatus) { Serial.println("No status yet, wait for first notification."); return; }
  Settings s = lastSettings;
  s.locked = locked ? 1 : 0;
  writeFrame(buildSetState(s));
}

// level: 0=L, 1=M, 2=H - confirmed against the real app UI (see reference doc)
void setBatteryProtection(int8_t level) {
  if (!haveStatus) { Serial.println("No status yet, wait for first notification."); return; }
  Settings s = lastSettings;
  s.hLvl = level;
  writeFrame(buildSetState(s));
}

void setTemperature(int8_t temp) {
  writeFrame(buildSetTemp(temp));
}

static void printStatus() {
  if (!haveStatus) { Serial.println("No status received yet."); return; }
  float voltage = lastSensors.inputV1 + (lastSensors.inputV2 / 10.0f);
  Serial.printf("On=%d Locked=%d Eco=%d HLvl=%d Unit=%s SetTemp=%d ActualTemp=%d Voltage=%.1fV\n",
                lastSettings.on, lastSettings.locked, lastSettings.ecoMode, lastSettings.hLvl,
                lastSettings.celsiusFahrenheitE5 ? "F" : "C",
                lastSettings.tempSet, lastSensors.temp, voltage);
}

// ---- BLE plumbing ----
static void notifyCallback(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t length, bool isNotify) {
  // Confirmed on real hardware: every command written to 1235 is echoed back here
  // first (same command code, shorter frame) before a fresh 24-byte status report
  // (command code 1) arrives. We just print+ignore anything shorter than a full
  // status report - it's an ack, not new state.
  if (length < sizeof(StatusReport)) {
    Serial.print("RX (echo/ack): ");
    for (size_t i = 0; i < length; i++) { if (data[i] < 0x10) Serial.print('0'); Serial.print(data[i], HEX); Serial.print(' '); }
    Serial.println();
    return;
  }
  StatusReport r;
  memcpy(&r, data, sizeof(r));
  // preamble/dataLen/commandCode not endian-sensitive for our purposes (single bytes / fe fe)
  lastSettings = r.settings;
  lastSensors = r.sensors;
  haveStatus = true;
  printStatus();
}

class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *c) override { Serial.println("Connected"); }
  void onDisconnect(NimBLEClient *c) override {
    Serial.println("Disconnected, will retry scan");
    connected = false;
  }
};

static bool nameMatches(const std::string &name) {
  for (int i = 0; i < NUM_PREFIXES; i++) {
    if (name.rfind(NAME_PREFIXES[i], 0) == 0) return true;
  }
  return false;
}

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    bool addressMatch = strlen(FRIDGE_BLE_ADDRESS) > 0 &&
                         dev->getAddress().toString() == FRIDGE_BLE_ADDRESS;
    bool nameMatch = dev->haveName() && nameMatches(dev->getName());
    if (addressMatch || (strlen(FRIDGE_BLE_ADDRESS) == 0 && nameMatch)) {
      Serial.printf("Found fridge: %s (%s)\n", dev->getName().c_str(), dev->getAddress().toString().c_str());
      NimBLEDevice::getScan()->stop();
      targetDevice = new NimBLEAdvertisedDevice(*dev);
      doConnect = true;
    }
  }
};

static bool connectToFridge() {
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCB(), false);

  if (!pClient->connect(targetDevice)) {
    Serial.println("Connect failed");
    return false;
  }

  NimBLERemoteService *svc = pClient->getService(SERVICE_UUID);
  if (svc == nullptr) { Serial.println("Service not found"); pClient->disconnect(); return false; }

  pWriteChar = svc->getCharacteristic(WRITE_UUID);
  pNotifyChar = svc->getCharacteristic(NOTIFY_UUID);
  if (pWriteChar == nullptr || pNotifyChar == nullptr) {
    Serial.println("Characteristics not found");
    pClient->disconnect();
    return false;
  }

  if (pNotifyChar->canNotify()) {
    pNotifyChar->subscribe(true, notifyCallback);
  }

  connected = true;
  Serial.println("Ready. Type: on / off / eco on / eco off / lock on / lock off / temp <n> / status");
  return true;
}

static uint32_t lastPing = 0;
static String serialBuf;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Scanning for fridge (A1-/AK1-/AK2-/AK3-)...");

  NimBLEDevice::init("esp32-fridge-ctl");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCB());
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(100);
  scan->start(10, false);
}

void loop() {
  if (doConnect) {
    doConnect = false;
    connectToFridge();
  }

  if (!connected && targetDevice == nullptr) {
    static uint32_t lastRescan = 0;
    if (millis() - lastRescan > 8000) {
      lastRescan = millis();
      NimBLEDevice::getScan()->start(5, false);
    }
  }

  if (connected && millis() - lastPing > 1500) {
    lastPing = millis();
    writeFrame(buildPing());
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuf.trim();
      if (serialBuf == "on") setPower(true);
      else if (serialBuf == "off") setPower(false);
      else if (serialBuf == "eco on") setEcoMode(true);
      else if (serialBuf == "eco off") setEcoMode(false);
      else if (serialBuf == "lock on") setLock(true);
      else if (serialBuf == "lock off") setLock(false);
      else if (serialBuf == "status") printStatus();
      else if (serialBuf.startsWith("temp ")) setTemperature((int8_t)serialBuf.substring(5).toInt());
      else if (serialBuf.length()) Serial.println("Unknown command");
      serialBuf = "";
    } else {
      serialBuf += c;
    }
  }
}

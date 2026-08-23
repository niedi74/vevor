#pragma once
/*
  Fridge BLE client — extracted/adapted from src/esp32_fridge_control.ino
  so it can be reused by the touch-panel UI (main.cpp) instead of a serial
  console. Protocol details: see docs/FRIDGE_BLE_PROTOCOL.md.
*/
#include <Arduino.h>
#include <functional>

#pragma pack(push, 1)
struct FridgeSettings {
  uint8_t locked;
  uint8_t on;
  uint8_t ecoMode;
  int8_t hLvl;      // 0=L 1=M 2=H
  int8_t tempSet;
  int8_t e2, e1, hysteresisE3, softStartE4;
  uint8_t celsiusFahrenheitE5; // 1 = F, 0 = C
  int8_t e6, e7, e8, e9;
};

struct FridgeSensors {
  int8_t temp;
  int8_t batteryPercent; // byte 1 of the sensor block; 0x64 = 100%
  int8_t inputV1;
  int8_t inputV2;
};
#pragma pack(pop)

enum class FridgeConnState { Scanning, Connecting, Connected, Disconnected };

// Called whenever a fresh 24-byte status report is parsed.
using FridgeStatusCallback = std::function<void(const FridgeSettings &, const FridgeSensors &)>;
// Called on connection-state changes so the UI can show scanning/connected/lost.
using FridgeConnCallback = std::function<void(FridgeConnState)>;

class FridgeBLE {
public:
  void begin(FridgeStatusCallback onStatus, FridgeConnCallback onConn);
  void loop(); // call every main-loop iteration; handles reconnect + keep-alive ping

  // State lives in fridge_ble.cpp (touched from the NimBLE callbacks), so
  // these are defined there rather than inline.
  bool haveStatus() const;
  const FridgeSettings &settings() const;
  const FridgeSensors &sensors() const;

  // Convenience setters — all read-modify-write the 14-byte settings block,
  // per protocol doc (§ "Status setzen" always echoes back the full block).
  void setPower(bool on);
  void setEcoMode(bool eco);
  void setLock(bool locked);
  void setBatteryProtection(int8_t level); // 0=L 1=M 2=H
  void setTemperature(int8_t temp);        // dedicated CommandCode 5 frame

private:
  void writeSettings(const FridgeSettings &s);
};

extern FridgeBLE Fridge;

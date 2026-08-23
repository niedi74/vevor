# ESP32 Touch-Bedienpanel – Code-Dokumentation

Vollständiger Quellcode des Touch-Bedienpanels für die BLE-Kühlbox
(Vevor/Setpower/Alpicool/BougeRV/ICECO, Board `WT-0001`), gedacht für ein
**Waveshare ESP32-S3-LCD-1.54"** (240×240, ST7789 + kapazitiver Touch).
Ersetzt die Original-Bedieneinheit bzw. die Handy-App, wenn die Kühlbox
z. B. im Schrank verbaut ist und ihr eigenes Bedienfeld nicht erreichbar
ist.

Diese Datei fasst alle Projektdateien an einem Ort zusammen (zum Lesen/
Review). Zum Bauen/Flashen wird die Ordnerstruktur benötigt – siehe
[README.md](../../README.md) für Setup-Schritte, oder das PlatformIO-Projekt
direkt (`src/esp32_touch_panel/`).

## Projektstruktur

```
src/esp32_touch_panel/
├── platformio.ini
├── include/
│   ├── display_config.h   Board-Pinbelegung (Display + Touch)
│   ├── fridge_ble.h        Schnittstelle zum BLE-Kühlbox-Client
│   └── lv_conf.h           LVGL-Konfiguration
└── src/
    ├── fridge_ble.cpp      BLE-Client (Scan, Verbinden, Protokoll)
    └── main.cpp            Display-Init + Touch-UI
```

## Funktionsumfang

- Ein/Aus
- Solltemperatur +/- einstellen
- Aktuelle (gemessene) Temperatur
- Batterie-% und Eingangsspannung
- Verbindungsstatus (Suche / Verbinde / Verbunden / Verbindung verloren)

ECO-Modus, Sperre und Batterieschutz-Stufe sind in `fridge_ble` bereits als
Funktionen vorhanden (`setEcoMode`, `setLock`, `setBatteryProtection`) –
für weitere Buttons in `main.cpp` einfach zusätzliche `lv_btn_create(...)`
mit passendem Callback ergänzen, analog zu `onPowerClicked`.

Protokoll-Referenz (GATT-UUIDs, Frame-Format, Prüfsumme, Settings-Byte-
Tabelle): [`docs/FRIDGE_BLE_PROTOCOL.md`](../../docs/FRIDGE_BLE_PROTOCOL.md)
im Repo-Root.

## Offene Punkte

- **Touch-Treiber ist ein Stub** (`touchRead()` in `main.cpp`) – der genaue
  I2C-Touch-Chip variiert je nach Board-Revision (meist CST816-Familie).
  Eine passende Bibliothek einbinden und dort die aktuelle Berührposition
  zurückgeben.
- **Pin-Zuordnung in `display_config.h` ist eine Annahme** basierend auf
  typischen ESP32-S3-LCD-1.54-Layouts – vor dem Flashen gegen die aktuelle
  Waveshare-Wiki-Pinout-Tabelle prüfen (Waveshare hat die Belegung zwischen
  Revisionen schon mehrfach geändert).
- Das offene "On"-Bit-Verhalten aus dem Hauptprotokoll gilt hier genauso
  (siehe `docs/FRIDGE_BLE_PROTOCOL.md#offene-punkte`).

---

## `platformio.ini`

```ini
; PlatformIO project for the small touch control panel (Waveshare
; ESP32-S3-LCD-1.54"). Build/upload with:  pio run -t upload -t monitor
[env:esp32-s3-touch-panel]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

; Waveshare's ESP32-S3-LCD-1.54 typically needs octal/QIO flash + PSRAM
; settings depending on the exact module revision — adjust if you get a
; boot loop, per the board's wiki "Arduino IDE" setup page.
board_build.arduino.memory_type = qio_opi

lib_deps =
    h2zero/NimBLE-Arduino @ ^1.4.1
    moononournation/GFX Library for Arduino @ ^1.4.7
    lvgl/lvgl @ ^8.3.11

build_flags =
    -D LV_CONF_INCLUDE_SIMPLE
    -I include
```

## `include/display_config.h`

Alle board-spezifischen Pins isoliert an einer Stelle – bei anderem Board
(z. B. AMOLED-Variante oder CYD) muss nur diese Datei angepasst werden.

```cpp
#pragma once
/*
  Display/touch pin mapping for the Waveshare ESP32-S3-LCD-1.54"
  (240x240, ST7789, capacitive touch).

  IMPORTANT: Waveshare has shipped more than one pin layout for this board
  over time. Before flashing, cross-check these against the pinout table on
  the current product wiki page for "ESP32-S3-LCD-1.54" and Waveshare's own
  demo (github.com/waveshareteam or the product's Wiki "Demo" download) —
  fix any mismatch here, nowhere else in the code needs to change.

  This file intentionally isolates ALL hardware-specific pins so main.cpp /
  fridge_ble.cpp never need touching if you swap to a different board
  (e.g. the 1.8" AMOLED variant, or a CYD) later.
*/

// ---- Display (SPI, ST7789) ----
#define PIN_LCD_SCLK   40
#define PIN_LCD_MOSI   45
#define PIN_LCD_MISO   -1   // not used, ST7789 is write-only
#define PIN_LCD_DC     41
#define PIN_LCD_CS     42
#define PIN_LCD_RST    39
#define PIN_LCD_BL     48   // backlight, active high

#define LCD_WIDTH      240
#define LCD_HEIGHT     240
#define LCD_ROTATION   0

// ---- Touch (I2C capacitive, e.g. CST816) ----
#define PIN_TOUCH_SDA  1
#define PIN_TOUCH_SCL  3
#define PIN_TOUCH_RST  2
#define PIN_TOUCH_INT  4
#define TOUCH_I2C_ADDR 0x15
```

## `include/lv_conf.h`

Minimale LVGL-v8-Konfiguration.

```cpp
/* Minimal LVGL v8 config for this project. Copy of the relevant defaults
   from lvgl's lv_conf_template.h, trimmed down. Adjust freely. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (48U * 1024U)

#define LV_TICK_CUSTOM     1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

#define LV_USE_LOG 0

#endif // LV_CONF_H
```

## `include/fridge_ble.h`

Öffentliche Schnittstelle des BLE-Kühlbox-Clients.

```cpp
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

  bool haveStatus() const { return _haveStatus; }
  const FridgeSettings &settings() const { return _settings; }
  const FridgeSensors &sensors() const { return _sensors; }

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
```

## `src/fridge_ble.cpp`

BLE-Client-Implementierung – Scan, Verbindungsaufbau, Frame-Aufbau/
Prüfsumme, Notify-Handling. Protokoll-Logik 1:1 aus
`src/esp32_fridge_control.ino` übernommen, nur als wiederverwendbares
Modul mit Callbacks statt serieller Konsole.

```cpp
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
```

## `src/main.cpp`

Display-/LVGL-Initialisierung und Touch-UI.

```cpp
/*
  Small touch control panel for the Vevor/Setpower/Alpicool BLE fridge,
  for a Waveshare ESP32-S3-LCD-1.54" (240x240, ST7789 + capacitive touch).

  Replaces the phone app for day-to-day use when the fridge's own control
  panel is physically inaccessible (e.g. mounted facing away, inside a
  cabinet). Talks BLE directly to the fridge — see docs/FRIDGE_BLE_PROTOCOL.md
  in the repo root and fridge_ble.h/.cpp for the protocol implementation.

  Libraries (install via PlatformIO, see platformio.ini):
    - NimBLE-Arduino
    - Arduino_GFX_Library   (display driver)
    - lvgl (v8.3.x)         (UI)

  UI, kept deliberately minimal per requirements:
    - ON/OFF toggle
    - Target temperature, +/- buttons
    - Current (measured) temperature
    - Battery % and input voltage
    - Connection status (scanning / connecting / connected / lost)
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "display_config.h"
#include "fridge_ble.h"

// ---------------- Display / LVGL plumbing ----------------
static Arduino_DataBus *bus = new Arduino_ESP32SPI(
    PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_MISO);
static Arduino_GFX *gfx = new Arduino_ST7789(
    bus, PIN_LCD_RST, LCD_ROTATION, true, LCD_WIDTH, LCD_HEIGHT);

static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf1[LCD_WIDTH * 40];
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

// Touch driver stub: wire this to your controller's I2C read (e.g. CST816).
// Returns true and fills x/y if a press is currently active.
static bool touchRead(int16_t *x, int16_t *y) {
  // TODO: implement using the touch chip on your board (commonly CST816 /
  // GT911 on this Waveshare model). See PIN_TOUCH_* in display_config.h.
  // Many touch libraries (e.g. "TAMCTec/cst816s" or Waveshare's own demo
  // driver) expose a getPoint(x,y) style call that fits directly here.
  return false;
}

static void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(drv);
}

static void touchpadRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t x, y;
  if (touchRead(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------------- UI widgets ----------------
static lv_obj_t *lblStatus;
static lv_obj_t *lblCurTemp;
static lv_obj_t *lblSetTemp;
static lv_obj_t *lblBattery;
static lv_obj_t *btnPower;
static lv_obj_t *btnPowerLabel;

static int8_t uiTargetTemp = 4; // shown before first status arrives

static void updatePowerButton(bool on) {
  if (on) {
    lv_obj_set_style_bg_color(btnPower, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(btnPowerLabel, "AN");
  } else {
    lv_obj_set_style_bg_color(btnPower, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(btnPowerLabel, "AUS");
  }
}

static void onPowerClicked(lv_event_t *e) {
  bool wantOn = !(Fridge.haveStatus() && Fridge.settings().on);
  Fridge.setPower(wantOn);
}

static void onTempPlus(lv_event_t *e) {
  uiTargetTemp++;
  Fridge.setTemperature(uiTargetTemp);
  lv_label_set_text_fmt(lblSetTemp, "%d°C", uiTargetTemp);
}

static void onTempMinus(lv_event_t *e) {
  uiTargetTemp--;
  Fridge.setTemperature(uiTargetTemp);
  lv_label_set_text_fmt(lblSetTemp, "%d°C", uiTargetTemp);
}

static lv_obj_t *makeRoundButton(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t size, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_radius(btn, size / 2, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  return btn;
}

static void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lblStatus = lv_label_create(scr);
  lv_obj_set_style_text_color(lblStatus, lv_color_white(), 0);
  lv_label_set_text(lblStatus, "Suche Kuehlbox...");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_MID, 0, 4);

  lblCurTemp = lv_label_create(scr);
  lv_obj_set_style_text_font(lblCurTemp, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblCurTemp, lv_color_white(), 0);
  lv_label_set_text(lblCurTemp, "--°C");
  lv_obj_align(lblCurTemp, LV_ALIGN_TOP_MID, 0, 28);

  // Power toggle
  btnPower = lv_btn_create(scr);
  lv_obj_set_size(btnPower, 100, 44);
  lv_obj_align(btnPower, LV_ALIGN_TOP_MID, 0, 74);
  lv_obj_add_event_cb(btnPower, onPowerClicked, LV_EVENT_CLICKED, NULL);
  btnPowerLabel = lv_label_create(btnPower);
  lv_label_set_text(btnPowerLabel, "AUS");
  lv_obj_center(btnPowerLabel);
  updatePowerButton(false);

  // Target temperature +/-
  makeRoundButton(scr, "-", 20, 130, 50, onTempMinus);
  makeRoundButton(scr, "+", 170, 130, 50, onTempPlus);

  lblSetTemp = lv_label_create(scr);
  lv_obj_set_style_text_font(lblSetTemp, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblSetTemp, lv_color_white(), 0);
  lv_label_set_text_fmt(lblSetTemp, "%d°C", uiTargetTemp);
  lv_obj_align(lblSetTemp, LV_ALIGN_TOP_MID, 0, 138);

  lblBattery = lv_label_create(scr);
  lv_obj_set_style_text_color(lblBattery, lv_color_white(), 0);
  lv_label_set_text(lblBattery, "Batterie: -- %  /  -- V");
  lv_obj_align(lblBattery, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ---------------- Fridge callbacks -> UI updates ----------------
static void onFridgeStatus(const FridgeSettings &s, const FridgeSensors &sens) {
  lv_label_set_text_fmt(lblCurTemp, "%d°C", sens.temp);
  uiTargetTemp = s.tempSet;
  lv_label_set_text_fmt(lblSetTemp, "%d°C", s.tempSet);
  float voltage = sens.inputV1 + (sens.inputV2 / 10.0f);
  lv_label_set_text_fmt(lblBattery, "Batterie: %d %%  /  %.1f V", sens.batteryPercent, voltage);
  updatePowerButton(s.on != 0);
}

static void onFridgeConn(FridgeConnState st) {
  switch (st) {
    case FridgeConnState::Scanning:    lv_label_set_text(lblStatus, "Suche Kuehlbox..."); break;
    case FridgeConnState::Connecting:  lv_label_set_text(lblStatus, "Verbinde..."); break;
    case FridgeConnState::Connected:   lv_label_set_text(lblStatus, "Verbunden"); break;
    case FridgeConnState::Disconnected:lv_label_set_text(lblStatus, "Verbindung verloren"); break;
  }
}

void setup() {
  Serial.begin(115200);

  gfx->begin();
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, lvBuf1, NULL, LCD_WIDTH * 40);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_WIDTH;
  dispDrv.ver_res = LCD_HEIGHT;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchpadRead;
  lv_indev_drv_register(&indevDrv);

  buildUI();

  Fridge.begin(onFridgeStatus, onFridgeConn);
}

void loop() {
  Fridge.loop();
  lv_timer_handler();
  delay(5);
}
```

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
  (void)x;
  (void)y;
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
  (void)e;
  bool wantOn = !(Fridge.haveStatus() && Fridge.settings().on);
  Fridge.setPower(wantOn);
}

static void onTempPlus(lv_event_t *e) {
  (void)e;
  uiTargetTemp++;
  Fridge.setTemperature(uiTargetTemp);
  lv_label_set_text_fmt(lblSetTemp, "%d°C", uiTargetTemp);
}

static void onTempMinus(lv_event_t *e) {
  (void)e;
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

/*
  Kühlbox-Bedienpanel auf Guition ESP32-4848S040
  4" 480×480, ST7701 RGB, GT911 Touch, ESP32-S3-N16R8

  Ersetzt die Handy-App für Vevor/Setpower/Alpicool-Kühlboxen (BLE, WT-0001).
  Eigenständig, kein ESPHome, kein Home Assistant.

  Funktionen:
    - Ein/Aus
    - Solltemperatur +/-
    - Aktuelle Temperatur (groß)
    - Batterie % und Eingangsspannung
    - ECO-Modus Ein/Aus
    - Verbindungsstatus

  Später erweiterbar um Victron-Daten (zweite BLE-Verbindung oder UART).
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <TAMC_GT911.h>
#include <esp_heap_caps.h>
#include "fridge_ble.h"

// =====================================================================
//  Hardware-Pins — Guition ESP32-4848S040
// =====================================================================

#define PIN_BL  38

#define TFT_CS  39
#define TFT_SCK 48
#define TFT_SDA 47

#define TFT_DE   18
#define TFT_VS   17
#define TFT_HS   16
#define TFT_PCLK 21

#define TFT_R0 11
#define TFT_R1 12
#define TFT_R2 13
#define TFT_R3 14
#define TFT_R4  0

#define TFT_G0  8
#define TFT_G1 20
#define TFT_G2  3
#define TFT_G3 46
#define TFT_G4  9
#define TFT_G5 10

#define TFT_B0  4
#define TFT_B1  5
#define TFT_B2  6
#define TFT_B3  7
#define TFT_B4 15

#define TP_SDA 19
#define TP_SCL 45
#define TP_INT -1
#define TP_RST -1

#define LCD_W 480
#define LCD_H 480

// =====================================================================
//  Display + Touch
// =====================================================================

Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, TFT_CS, TFT_SCK, TFT_SDA, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VS, TFT_HS, TFT_PCLK,
    TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
    1, 10, 8, 50,
    1, 10, 8, 20,
    0, 13000000, false);

Arduino_RGB_Display *tft = new Arduino_RGB_Display(
    LCD_W, LCD_H, rgbpanel, 0, false,
    bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

TAMC_GT911 touchPanel(TP_SDA, TP_SCL, TP_INT, TP_RST, LCD_W, LCD_H);

// =====================================================================
//  LVGL Plumbing
// =====================================================================

static lv_disp_draw_buf_t drawBuf;
static lv_color_t *lvBuf = nullptr;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

static void lvFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  tft->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full,
      area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  tft->flush();
  lv_disp_flush_ready(drv);
}

static void lvTouchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  touchPanel.read();
  if (touchPanel.isTouched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchPanel.points[0].x;
    data->point.y = touchPanel.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// =====================================================================
//  UI — Kühlbox-Steuerung
// =====================================================================

// Farben
#define COL_BG       0x0f0f1a
#define COL_CARD     0x1e1e30
#define COL_TEXT     0xffffff
#define COL_DIM      0x888899
#define COL_ACCENT   0x4488ff
#define COL_GREEN    0x22cc66
#define COL_RED      0xff4444
#define COL_ORANGE   0xff8833
#define COL_CYAN     0x00cccc

static lv_obj_t *lblStatus;
static lv_obj_t *lblCurTemp;
static lv_obj_t *lblCurTempUnit;
static lv_obj_t *lblSetTemp;
static lv_obj_t *lblBatPct;
static lv_obj_t *lblVoltage;
static lv_obj_t *btnPower;
static lv_obj_t *lblPower;
static lv_obj_t *btnEco;
static lv_obj_t *lblEco;

static int8_t uiTargetTemp = 4;

// ---- Hilfsfunktionen ----

static lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

// ---- Callbacks ----

static void onPowerClicked(lv_event_t *e) {
  (void)e;
  bool wantOn = !(Fridge.haveStatus() && Fridge.settings().on);
  Fridge.setPower(wantOn);
}

static void onEcoClicked(lv_event_t *e) {
  (void)e;
  if (!Fridge.haveStatus()) return;
  Fridge.setEcoMode(!Fridge.settings().ecoMode);
}

static void onTempPlus(lv_event_t *e) {
  (void)e;
  if (uiTargetTemp < 20) uiTargetTemp++;
  Fridge.setTemperature(uiTargetTemp);
  lv_label_set_text_fmt(lblSetTemp, "%d", uiTargetTemp);
}

static void onTempMinus(lv_event_t *e) {
  (void)e;
  if (uiTargetTemp > -20) uiTargetTemp--;
  Fridge.setTemperature(uiTargetTemp);
  lv_label_set_text_fmt(lblSetTemp, "%d", uiTargetTemp);
}

// ---- UI aufbauen ----

static void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);

  // -- Status-Leiste oben --
  lblStatus = lv_label_create(scr);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_ORANGE), 0);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblStatus, LV_SYMBOL_BLUETOOTH " Suche Kuehlbox...");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_MID, 0, 12);

  // -- Aktuelle Temperatur (gross, Mitte oben) --
  lv_obj_t *cardTemp = makeCard(scr, 40, 50, 400, 140);
  lv_obj_set_style_bg_color(cardTemp, lv_color_hex(COL_BG), 0);

  lv_obj_t *lblTempTitle = lv_label_create(cardTemp);
  lv_obj_set_style_text_color(lblTempTitle, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lblTempTitle, "Temperatur");
  lv_obj_align(lblTempTitle, LV_ALIGN_TOP_MID, 0, 0);

  lblCurTemp = lv_label_create(cardTemp);
  lv_obj_set_style_text_font(lblCurTemp, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(lblCurTemp, lv_color_hex(COL_CYAN), 0);
  lv_label_set_text(lblCurTemp, "--");
  lv_obj_align(lblCurTemp, LV_ALIGN_CENTER, -15, 10);

  lblCurTempUnit = lv_label_create(cardTemp);
  lv_obj_set_style_text_font(lblCurTempUnit, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblCurTempUnit, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lblCurTempUnit, "\xC2\xB0""C");
  lv_obj_align_to(lblCurTempUnit, lblCurTemp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);

  // -- Power Button --
  btnPower = lv_btn_create(scr);
  lv_obj_set_size(btnPower, 190, 70);
  lv_obj_set_pos(btnPower, 25, 200);
  lv_obj_set_style_radius(btnPower, 16, 0);
  lv_obj_set_style_bg_color(btnPower, lv_color_hex(COL_RED), 0);
  lv_obj_add_event_cb(btnPower, onPowerClicked, LV_EVENT_CLICKED, NULL);
  lblPower = lv_label_create(btnPower);
  lv_obj_set_style_text_font(lblPower, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblPower, LV_SYMBOL_POWER " AUS");
  lv_obj_center(lblPower);

  // -- ECO Button --
  btnEco = lv_btn_create(scr);
  lv_obj_set_size(btnEco, 190, 70);
  lv_obj_set_pos(btnEco, 265, 200);
  lv_obj_set_style_radius(btnEco, 16, 0);
  lv_obj_set_style_bg_color(btnEco, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(btnEco, onEcoClicked, LV_EVENT_CLICKED, NULL);
  lblEco = lv_label_create(btnEco);
  lv_obj_set_style_text_font(lblEco, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblEco, "ECO AUS");
  lv_obj_set_style_text_color(lblEco, lv_color_hex(COL_DIM), 0);
  lv_obj_center(lblEco);

  // -- Solltemperatur mit +/- --
  lv_obj_t *cardSet = makeCard(scr, 40, 290, 400, 100);

  lv_obj_t *lblSetTitle = lv_label_create(cardSet);
  lv_obj_set_style_text_color(lblSetTitle, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lblSetTitle, "Soll");
  lv_obj_align(lblSetTitle, LV_ALIGN_LEFT_MID, 0, 0);

  // Minus
  lv_obj_t *btnM = lv_btn_create(cardSet);
  lv_obj_set_size(btnM, 64, 64);
  lv_obj_align(btnM, LV_ALIGN_LEFT_MID, 60, 0);
  lv_obj_set_style_radius(btnM, 32, 0);
  lv_obj_set_style_bg_color(btnM, lv_color_hex(COL_ACCENT), 0);
  lv_obj_add_event_cb(btnM, onTempMinus, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lm = lv_label_create(btnM);
  lv_obj_set_style_text_font(lm, &lv_font_montserrat_28, 0);
  lv_label_set_text(lm, LV_SYMBOL_MINUS);
  lv_obj_center(lm);

  // Anzeige
  lblSetTemp = lv_label_create(cardSet);
  lv_obj_set_style_text_font(lblSetTemp, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(lblSetTemp, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text_fmt(lblSetTemp, "%d", uiTargetTemp);
  lv_obj_align(lblSetTemp, LV_ALIGN_CENTER, 10, 0);

  lv_obj_t *lblSetUnit = lv_label_create(cardSet);
  lv_obj_set_style_text_color(lblSetUnit, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(lblSetUnit, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblSetUnit, "\xC2\xB0""C");
  lv_obj_align_to(lblSetUnit, lblSetTemp, LV_ALIGN_OUT_RIGHT_TOP, 2, 6);

  // Plus
  lv_obj_t *btnP = lv_btn_create(cardSet);
  lv_obj_set_size(btnP, 64, 64);
  lv_obj_align(btnP, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_radius(btnP, 32, 0);
  lv_obj_set_style_bg_color(btnP, lv_color_hex(COL_ACCENT), 0);
  lv_obj_add_event_cb(btnP, onTempPlus, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lp = lv_label_create(btnP);
  lv_obj_set_style_text_font(lp, &lv_font_montserrat_28, 0);
  lv_label_set_text(lp, LV_SYMBOL_PLUS);
  lv_obj_center(lp);

  // -- Batterie + Spannung (unten) --
  lv_obj_t *cardBat = makeCard(scr, 25, 405, 210, 60);
  lv_obj_t *batIcon = lv_label_create(cardBat);
  lv_label_set_text(batIcon, LV_SYMBOL_BATTERY_FULL);
  lv_obj_set_style_text_color(batIcon, lv_color_hex(COL_GREEN), 0);
  lv_obj_align(batIcon, LV_ALIGN_LEFT_MID, 0, 0);

  lblBatPct = lv_label_create(cardBat);
  lv_obj_set_style_text_font(lblBatPct, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblBatPct, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lblBatPct, "-- %");
  lv_obj_align(lblBatPct, LV_ALIGN_LEFT_MID, 30, 0);

  lv_obj_t *cardVolt = makeCard(scr, 245, 405, 210, 60);
  lv_obj_t *voltIcon = lv_label_create(cardVolt);
  lv_label_set_text(voltIcon, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(voltIcon, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(voltIcon, LV_ALIGN_LEFT_MID, 0, 0);

  lblVoltage = lv_label_create(cardVolt);
  lv_obj_set_style_text_font(lblVoltage, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblVoltage, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lblVoltage, "-- V");
  lv_obj_align(lblVoltage, LV_ALIGN_LEFT_MID, 30, 0);
}

// =====================================================================
//  Fridge BLE → UI Updates
// =====================================================================

static void onFridgeStatus(const FridgeSettings &s, const FridgeSensors &sens) {
  // Ist-Temperatur
  lv_label_set_text_fmt(lblCurTemp, "%d", sens.temp);

  // Solltemperatur
  uiTargetTemp = s.tempSet;
  lv_label_set_text_fmt(lblSetTemp, "%d", s.tempSet);

  // Power-Button
  if (s.on) {
    lv_obj_set_style_bg_color(btnPower, lv_color_hex(COL_GREEN), 0);
    lv_label_set_text(lblPower, LV_SYMBOL_POWER " AN");
  } else {
    lv_obj_set_style_bg_color(btnPower, lv_color_hex(COL_RED), 0);
    lv_label_set_text(lblPower, LV_SYMBOL_POWER " AUS");
  }

  // ECO-Button
  if (s.ecoMode) {
    lv_obj_set_style_bg_color(btnEco, lv_color_hex(COL_GREEN), 0);
    lv_label_set_text(lblEco, LV_SYMBOL_OK " ECO AN");
    lv_obj_set_style_text_color(lblEco, lv_color_hex(COL_TEXT), 0);
  } else {
    lv_obj_set_style_bg_color(btnEco, lv_color_hex(COL_CARD), 0);
    lv_label_set_text(lblEco, "ECO AUS");
    lv_obj_set_style_text_color(lblEco, lv_color_hex(COL_DIM), 0);
  }

  // Batterie
  lv_label_set_text_fmt(lblBatPct, "%d %%", sens.batteryPercent);
  float voltage = sens.inputV1 + (sens.inputV2 / 10.0f);
  lv_label_set_text_fmt(lblVoltage, "%.1f V", voltage);
}

static void onFridgeConn(FridgeConnState st) {
  switch (st) {
    case FridgeConnState::Scanning:
      lv_label_set_text(lblStatus, LV_SYMBOL_BLUETOOTH " Suche Kuehlbox...");
      lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_ORANGE), 0);
      break;
    case FridgeConnState::Connecting:
      lv_label_set_text(lblStatus, LV_SYMBOL_BLUETOOTH " Verbinde...");
      lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_ORANGE), 0);
      break;
    case FridgeConnState::Connected:
      lv_label_set_text(lblStatus, LV_SYMBOL_BLUETOOTH " Verbunden");
      lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_GREEN), 0);
      break;
    case FridgeConnState::Disconnected:
      lv_label_set_text(lblStatus, LV_SYMBOL_WARNING " Verbindung verloren");
      lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_RED), 0);
      break;
  }
}

// =====================================================================
//  setup() / loop()
// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Kuehlbox-Panel (Guition 4848S040) ===");
  Serial.printf("PSRAM frei: %d KB\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10);

  // Backlight
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  // Display
  tft->begin();
  tft->fillScreen(BLACK);
  tft->flush();

  // Touch
  touchPanel.begin();
  touchPanel.setRotation(1);

  // LVGL
  lv_init();
  size_t bufPx = LCD_W * 40;
  lvBuf = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!lvBuf) lvBuf = (lv_color_t *)malloc(bufPx * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, bufPx);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_W;
  dispDrv.ver_res = LCD_H;
  dispDrv.flush_cb = lvFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = lvTouchRead;
  lv_indev_drv_register(&indevDrv);

  buildUI();

  // BLE starten
  Fridge.begin(onFridgeStatus, onFridgeConn);
  Serial.println("BLE Scan gestartet, UI bereit.");
}

void loop() {
  Fridge.loop();
  lv_timer_handler();
  delay(5);
}

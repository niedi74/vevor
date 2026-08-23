/*
  Display-Test für Guition ESP32-4848S040
  4" 480×480, ST7701 (parallel RGB), GT911 kapazitiver Touch, ESP32-S3-N16R8

  Zeigt:
  1. Farbflächen (Rot, Grün, Blau, Weiß) zum Prüfen der Farbkanäle
  2. LVGL-Testscreen mit Touch-Koordinaten-Anzeige
  3. Touch-Events auf Serial (115200)

  Pin-Referenz: https://github.com/alaltitov/Guition-ESP32-S3-4848S040
                https://github.com/moononournation/Arduino_GFX/issues/684
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>

// ---- Pin-Definitionen Guition ESP32-4848S040 ----

// Backlight (GPIO38, NOT 39 — 39 is SPI CS for ST7701 init!)
// Verified via https://github.com/alaltitov/Guition-ESP32-S3-4848S040
#define PIN_BL   38

// ST7701 SPI-Init-Bus (Software-SPI, nur für Register-Init, nicht für Pixeldaten)
#define PIN_SPI_CS   39
#define PIN_SPI_SCK  48
#define PIN_SPI_MOSI 47

// RGB Parallel-Datenpins
#define PIN_DE    18
#define PIN_VSYNC 17
#define PIN_HSYNC 16
#define PIN_PCLK  21

// R[4:0], G[5:0], B[4:0]
#define PIN_R0 11
#define PIN_R1 12
#define PIN_R2 13
#define PIN_R3 14
#define PIN_R4  0

#define PIN_G0  8
#define PIN_G1 20
#define PIN_G2  3
#define PIN_G3 46
#define PIN_G4  9
#define PIN_G5 10

#define PIN_B0  4
#define PIN_B1  5
#define PIN_B2  6
#define PIN_B3  7
#define PIN_B4 15

// Touch GT911 (I2C)
#define PIN_TOUCH_SDA  19
#define PIN_TOUCH_SCL  45
#define PIN_TOUCH_INT  -1
#define PIN_TOUCH_RST  -1
#define GT911_ADDR     0x5D   // oder 0x14 je nach Board-Revision

#define LCD_WIDTH  480
#define LCD_HEIGHT 480

// ---- Display-Setup ----
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, PIN_SPI_CS,
    PIN_SPI_SCK, PIN_SPI_MOSI, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    PIN_DE, PIN_VSYNC, PIN_HSYNC, PIN_PCLK,
    PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
    PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
    PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4,
    1, 10, 8, 50,    // hsync: polarity, front_porch, pulse_width, back_porch
    1, 10, 8, 20);   // vsync: polarity, front_porch, pulse_width, back_porch

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbpanel, 0, true,
    bus, GFX_NOT_DEFINED,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

// ---- LVGL ----
static lv_disp_draw_buf_t drawBuf;
static lv_color_t *lvBuf1 = nullptr;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

// ---- GT911 minimaler Touch-Treiber ----
static bool gt911_read(int16_t *x, int16_t *y) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81);  // register high byte
  Wire.write(0x4E);  // register low byte = 0x814E (touch status)
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  uint8_t status = Wire.read();

  uint8_t touches = status & 0x0F;
  if (!(status & 0x80) || touches == 0 || touches > 5) {
    // Clear status flag
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();
    return false;
  }

  // Read first touch point (0x8150..0x8153 = x_lo, x_hi, y_lo, y_hi)
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x50);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)4);
  if (Wire.available() >= 4) {
    uint8_t xl = Wire.read(), xh = Wire.read();
    uint8_t yl = Wire.read(), yh = Wire.read();
    *x = (xh << 8) | xl;
    *y = (yh << 8) | yl;
  }

  // Clear status flag
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
  Wire.endTransmission();
  return true;
}

// ---- LVGL Callbacks ----
static void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(drv);
}

static void touchpadRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t x, y;
  if (gt911_read(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---- UI ----
static lv_obj_t *lblTouch;
static lv_obj_t *lblInfo;

static void buildTestUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

  // Titel
  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "ESP32-4848S040 Display-Test");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // Info
  lblInfo = lv_label_create(scr);
  lv_obj_set_style_text_color(lblInfo, lv_color_hex(0xaaaaaa), 0);
  lv_label_set_text(lblInfo, "Tippe auf den Bildschirm...");
  lv_obj_align(lblInfo, LV_ALIGN_TOP_MID, 0, 60);

  // Touch-Koordinaten gross anzeigen
  lblTouch = lv_label_create(scr);
  lv_obj_set_style_text_font(lblTouch, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblTouch, lv_color_hex(0x00ff88), 0);
  lv_label_set_text(lblTouch, "X: ---  Y: ---");
  lv_obj_align(lblTouch, LV_ALIGN_CENTER, 0, -20);

  // 4 Farb-Kacheln zum Testen der RGB-Kanäle
  static const struct { uint32_t color; const char *name; } tiles[] = {
    {0xff0000, "R"}, {0x00ff00, "G"}, {0x0000ff, "B"}, {0xffffff, "W"}
  };
  for (int i = 0; i < 4; i++) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 100, 100);
    lv_obj_set_pos(box, 20 + i * 115, 340);
    lv_obj_set_style_bg_color(box, lv_color_hex(tiles[i].color), 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_border_width(box, 0, 0);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, tiles[i].name);
    lv_obj_set_style_text_color(lbl, tiles[i].color == 0xffffff ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(lbl);
  }
}

// ---- Farbtest ohne LVGL (direkt auf GFX) ----
static void rawColorTest() {
  Serial.println("Farbtest: ROT");
  gfx->fillScreen(RED);    delay(800);
  Serial.println("Farbtest: GRUEN");
  gfx->fillScreen(GREEN);  delay(800);
  Serial.println("Farbtest: BLAU");
  gfx->fillScreen(BLUE);   delay(800);
  Serial.println("Farbtest: WEISS");
  gfx->fillScreen(WHITE);  delay(800);
  Serial.println("Farbtest fertig, starte LVGL...");
  gfx->fillScreen(BLACK);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-4848S040 Display-Test ===");

  // Backlight an
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  // Display init
  Serial.println("Display init...");
  gfx->begin();
  Serial.println("Display init OK");

  // Roh-Farbtest (ohne LVGL)
  rawColorTest();

  // Touch I2C init
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(100000);  // 100 kHz — GT911 ist damit stabiler (alaltitov-Referenz)
  Serial.print("GT911 I2C Scan: ");
  Wire.beginTransmission(GT911_ADDR);
  Serial.println(Wire.endTransmission() == 0 ? "GEFUNDEN" : "NICHT GEFUNDEN — pruefe Adresse/Pins!");

  // LVGL init
  lv_init();
  size_t bufSize = LCD_WIDTH * 40;
  lvBuf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!lvBuf1) {
    Serial.println("FEHLER: PSRAM-Alloc fehlgeschlagen, nutze internen RAM");
    lvBuf1 = (lv_color_t *)malloc(bufSize * sizeof(lv_color_t));
  } else {
    Serial.println("LVGL-Buffer in PSRAM alloziert");
  }
  lv_disp_draw_buf_init(&drawBuf, lvBuf1, NULL, bufSize);

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

  buildTestUI();
  Serial.println("LVGL UI gestartet. Tippe auf das Display!");
}

static uint32_t lastTouchPrint = 0;

void loop() {
  lv_timer_handler();

  // Touch-Koordinaten auf Serial + Label aktualisieren
  int16_t x, y;
  if (gt911_read(&x, &y)) {
    lv_label_set_text_fmt(lblTouch, "X: %d  Y: %d", x, y);
    if (millis() - lastTouchPrint > 200) {
      lastTouchPrint = millis();
      Serial.printf("Touch: x=%d y=%d\n", x, y);
    }
  }

  delay(5);
}

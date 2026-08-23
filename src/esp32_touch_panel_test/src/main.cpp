/*
  Display-Test für Guition ESP32-4848S040
  4" 480×480, ST7701 (parallel RGB), GT911 kapazitiver Touch, ESP32-S3-N16R8

  Zeigt:
  1. Farbflächen (Rot → Grün → Blau → Weiß) zum Prüfen der Farbkanäle
  2. LVGL-Testscreen mit Touch-Koordinaten und Farbkacheln
  3. Touch-Events auf Serial (115200)

  Hardware-Referenz:
    https://github.com/ha5dzs/Guition-ESP32-4848S040-platformio
    https://github.com/alaltitov/Guition-ESP32-S3-4848S040
    https://github.com/moononournation/Arduino_GFX/issues/684
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <TAMC_GT911.h>
#include <esp_heap_caps.h>

// =====================================================================
//  Pin-Definitionen — Guition ESP32-4848S040
//  Quelle: ha5dzs/Guition_ESP32_4848S040.h (gegen Schaltplan verifiziert)
// =====================================================================

// Backlight (Boost-Converter, kein PWM-Dimming möglich)
#define PIN_BL  38

// ST7701 SPI-Init-Bus (Software-SPI, nur für Register-Init)
#define TFT_CS  39
#define TFT_SCK 48
#define TFT_SDA 47

// RGB Parallel-Interface
#define TFT_DE    18
#define TFT_VS    17
#define TFT_HS    16
#define TFT_PCLK  21

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

// GT911 Touch (I2C)
#define TP_SDA 19
#define TP_SCL 45
#define TP_INT -1
#define TP_RST -1

#define LCD_W 480
#define LCD_H 480

// =====================================================================
//  Display-Objekte
// =====================================================================

Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, TFT_CS, TFT_SCK, TFT_SDA, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VS, TFT_HS, TFT_PCLK,
    TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
    1, 10, 8, 50,   // hsync
    1, 10, 8, 20,   // vsync
    0,               // pclk active neg
    13000000,        // 13 MHz pixel clock (stabil, kein Glitching)
    false);          // big endian = false

Arduino_RGB_Display *tft = new Arduino_RGB_Display(
    LCD_W, LCD_H, rgbpanel, 0, false /* auto_flush = false! */,
    bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

// =====================================================================
//  GT911 Touch
// =====================================================================

TAMC_GT911 touch(TP_SDA, TP_SCL, TP_INT, TP_RST, LCD_W, LCD_H);

// =====================================================================
//  LVGL
// =====================================================================

static lv_disp_draw_buf_t drawBuf;
static lv_color_t *lvBuf = nullptr;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

static void lvFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  tft->flush();
  lv_disp_flush_ready(drv);
}

static void lvTouchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  touch.read();
  if (touch.isTouched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// =====================================================================
//  Test-UI
// =====================================================================

static lv_obj_t *lblTouch;
static lv_obj_t *lblInfo;
static uint32_t touchCount = 0;

static void buildTestUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

  // Titel
  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Guition 4848S040 Test");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // PSRAM Info
  lblInfo = lv_label_create(scr);
  lv_obj_set_style_text_color(lblInfo, lv_color_hex(0xaaaaaa), 0);
  lv_obj_align(lblInfo, LV_ALIGN_TOP_MID, 0, 60);
  lv_label_set_text_fmt(lblInfo, "PSRAM frei: %d KB", 
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10);

  // Touch-Koordinaten
  lblTouch = lv_label_create(scr);
  lv_obj_set_style_text_font(lblTouch, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblTouch, lv_color_hex(0x00ff88), 0);
  lv_label_set_text(lblTouch, "Tippe auf den Bildschirm...");
  lv_obj_align(lblTouch, LV_ALIGN_CENTER, 0, -20);

  // 4 Farb-Kacheln
  static const struct { uint32_t c; const char *n; } tiles[] = {
    {0xff0000, "R"}, {0x00ff00, "G"}, {0x0000ff, "B"}, {0xffffff, "W"}
  };
  for (int i = 0; i < 4; i++) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 100, 100);
    lv_obj_set_pos(box, 20 + i * 115, 340);
    lv_obj_set_style_bg_color(box, lv_color_hex(tiles[i].c), 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, tiles[i].n);
    lv_obj_set_style_text_color(lbl,
        tiles[i].c == 0xffffff ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(lbl);
  }

  // Anleitung
  lv_obj_t *hint = lv_label_create(scr);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
  lv_label_set_text(hint, "Farben falsch? -> st7701_type1 bis type9 probieren");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// =====================================================================
//  Roh-Farbtest (ohne LVGL)
// =====================================================================

static void rawColorTest() {
  Serial.println("Farbtest: ROT");
  tft->fillScreen(RED);   tft->flush(); delay(700);
  Serial.println("Farbtest: GRUEN");
  tft->fillScreen(GREEN); tft->flush(); delay(700);
  Serial.println("Farbtest: BLAU");
  tft->fillScreen(BLUE);  tft->flush(); delay(700);
  Serial.println("Farbtest: WEISS");
  tft->fillScreen(WHITE); tft->flush(); delay(700);
  Serial.println("Farbtest fertig.");
  tft->fillScreen(BLACK); tft->flush();
}

// =====================================================================
//  setup() / loop()
// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Guition ESP32-4848S040 Display-Test ===");
  Serial.printf("PSRAM frei: %d KB\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10);

  // Backlight
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  // Display
  Serial.println("Display init...");
  tft->begin();
  Serial.println("Display init OK");

  rawColorTest();

  // Touch
  touch.begin();
  touch.setRotation(1);  // Rotation 0 auf dem Board = TAMC Rotation 1
  Serial.println("GT911 Touch init OK");

  // LVGL
  lv_init();
  size_t bufPixels = LCD_W * 40;  // 40 Zeilen à 480px
  lvBuf = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (lvBuf) {
    Serial.printf("LVGL-Buffer: %d KB in PSRAM\n", (bufPixels * sizeof(lv_color_t)) >> 10);
  } else {
    Serial.println("WARNUNG: PSRAM-Alloc fehlgeschlagen, nutze internen RAM");
    lvBuf = (lv_color_t *)malloc(bufPixels * sizeof(lv_color_t));
  }
  lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, bufPixels);

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

  buildTestUI();
  Serial.println("LVGL UI gestartet — tippe auf das Display!");
}

void loop() {
  lv_timer_handler();

  // Touch auf Serial loggen
  touch.read();
  if (touch.isTouched) {
    touchCount++;
    if (touchCount % 10 == 1) {  // nicht jeden Frame loggen
      Serial.printf("Touch #%d: x=%d y=%d\n",
          touchCount, touch.points[0].x, touch.points[0].y);
    }
    lv_label_set_text_fmt(lblTouch, "X: %d  Y: %d  (#%d)",
        touch.points[0].x, touch.points[0].y, touchCount);
  }

  delay(5);
}

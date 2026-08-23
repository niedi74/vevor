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

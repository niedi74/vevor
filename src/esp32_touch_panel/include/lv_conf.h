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

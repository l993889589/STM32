#ifndef LV_CONF_H
#define LV_CONF_H

/* Minimal LVGL v9 configuration for STM32H563 + ST7796 RGB565 panel. */

#include <stdint.h>

#define LV_COLOR_DEPTH                  16

#define LV_USE_STDLIB_MALLOC            LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING            LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF           LV_STDLIB_BUILTIN
#define LV_MEM_SIZE                     (96U * 1024U)

#define LV_USE_OS                       LV_OS_NONE
#define LV_DRAW_SW_DRAW_UNIT_CNT        1
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1

#define LV_USE_LOG                      0
#define LV_USE_ASSERT_NULL              0
#define LV_USE_ASSERT_MALLOC            0
#define LV_USE_ASSERT_STYLE             0
#define LV_USE_ASSERT_MEM_INTEGRITY     0
#define LV_USE_ASSERT_OBJ               0

#define LV_USE_PERF_MONITOR             0
#define LV_USE_MEM_MONITOR              0

#define LV_FONT_MONTSERRAT_12           1
#define LV_FONT_MONTSERRAT_14           1
#define LV_FONT_MONTSERRAT_16           1
#define LV_FONT_MONTSERRAT_18           1
#define LV_FONT_MONTSERRAT_20           1
#define LV_FONT_MONTSERRAT_24           1
#define LV_FONT_MONTSERRAT_28           1
#define LV_FONT_DEFAULT                 &lv_font_montserrat_16

#define LV_USE_THEME_DEFAULT            1
#define LV_USE_FLEX                     1
#define LV_USE_GRID                     1

#define LV_USE_DEMO_WIDGETS             0
#define LV_USE_DEMO_BENCHMARK           0
#define LV_USE_DEMO_STRESS              0
#define LV_USE_DEMO_MUSIC               0

#endif /* LV_CONF_H */

#pragma once

#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Couleurs — identique LVGL_Widgets (NV3041A QSPI, LV_COLOR_16_SWAP + BeRGB flush) */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_COLOR_MIX_ROUND_OFS (LV_COLOR_DEPTH == 32 ? 0 : 128)

#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DPI_DEF 130

#define LV_USE_LOG 0

#define LV_DRAW_COMPLEX 1
#if LV_DRAW_COMPLEX != 0
#define LV_CIRCLE_CACHE_SIZE 4
#endif

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_SUBPX 0

#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_LED 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#define LV_USE_ANIMIMG 0

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#endif

#define LV_USE_IMG 1

#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif
#endif

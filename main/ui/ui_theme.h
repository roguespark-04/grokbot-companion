/**
 * @file ui_theme.h
 * @brief Layout constants + palette for the 466×466 round AMOLED.
 */
#pragma once

#include "lvgl.h"

#define UI_W            466
#define UI_H            466
#define UI_CX           (UI_W / 2)
#define UI_CY           (UI_H / 2)

/* Gesture tuning */
#define UI_EDGE_TOP     90      /* swipe-down must start above this y */
#define UI_EDGE_BOTTOM  (UI_H - 90)   /* swipe-up must start below this y */
#define UI_SWIPE_MIN    60      /* px for a committed swipe */
#define UI_SWIPE_VEL    600     /* px/s flick commits even when short */
#define UI_AXIS_LOCK    12      /* px before an axis is chosen */

/* Carousel */
#define UI_CAROUSEL_SPACING  200   /* px between bot slots (neighbors peek at the round edge) */
#define UI_CAROUSEL_SLOTS    5     /* -2..+2 so a full drag step stays filled */
#define UI_CAROUSEL_Y        178   /* avatar center y */
#define UI_AVATAR_BOX        250   /* avatar root (room for ripples) */
#define UI_AVATAR_BODY       150   /* body size at scale 1.0 */
#define UI_PEEK_SCALE        140   /* neighbor scale (256 = 1.0) */
#define UI_PEEK_OPA          110

/* Palette (AMOLED: true black background saves power) */
#define UI_COL_BG        lv_color_hex(0x000000)
#define UI_COL_SURFACE   lv_color_hex(0x15171C)
#define UI_COL_SURFACE2  lv_color_hex(0x22252D)
#define UI_COL_TEXT      lv_color_hex(0xF4F5F7)
#define UI_COL_MUTED     lv_color_hex(0x9AA0AA)
#define UI_COL_DIM       lv_color_hex(0x5B616B)
#define UI_COL_OK        lv_color_hex(0x22C55E)
#define UI_COL_WARN      lv_color_hex(0xF59E0B)
#define UI_COL_ERR       lv_color_hex(0xEF4444)
#define UI_COL_ACCENT    lv_color_hex(0x60A5FA)

/* Fonts (enable in sdkconfig.defaults: CONFIG_LV_FONT_MONTSERRAT_14/16/20/28) */
#define UI_FONT_SMALL    (&lv_font_montserrat_14)
#define UI_FONT_BODY     (&lv_font_montserrat_16)
#define UI_FONT_TITLE    (&lv_font_montserrat_20)
#define UI_FONT_NAME     (&lv_font_montserrat_28)

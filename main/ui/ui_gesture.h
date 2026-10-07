/**
 * @file ui_gesture.h
 * @brief Raw-pointer swipe detector with axis lock, edge-start info and live drag.
 *
 * LVGL's built-in LV_EVENT_GESTURE doesn't report where a swipe started, which we need
 * for "swipe up from the bottom edge" / "swipe down from the top edge", and it can't
 * drive a live carousel drag. This tracks PRESSED/PRESSING/RELEASED on one object.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SWIPE_NONE = 0,
    UI_SWIPE_LEFT,
    UI_SWIPE_RIGHT,
    UI_SWIPE_UP,
    UI_SWIPE_DOWN,
} ui_swipe_dir_t;

typedef enum {
    UI_AXIS_NONE = 0,
    UI_AXIS_H,
    UI_AXIS_V,
} ui_axis_t;

typedef struct {
    lv_point_t     start;      /* screen coords at press */
    int32_t        dx, dy;     /* total movement */
    int32_t        vx, vy;     /* release velocity px/s (signed) */
    ui_axis_t      axis;
    ui_swipe_dir_t dir;        /* committed direction, NONE if below thresholds */
} ui_gesture_info_t;

typedef struct {
    void (*on_press)(const ui_gesture_info_t *g, void *user);
    void (*on_drag)(const ui_gesture_info_t *g, void *user);     /* every PRESSING once axis locked */
    void (*on_release)(const ui_gesture_info_t *g, void *user);  /* swipe or tap (dir NONE) */
    void *user;
} ui_gesture_cbs_t;

/** Attach to obj (obj must be CLICKABLE). cbs is copied. */
void ui_gesture_attach(lv_obj_t *obj, const ui_gesture_cbs_t *cbs);

#ifdef __cplusplus
}
#endif

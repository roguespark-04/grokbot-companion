#include "ui_gesture.h"

#include <stdlib.h>
#include "ui_theme.h"

typedef struct {
    ui_gesture_cbs_t cbs;
    ui_gesture_info_t g;
    lv_point_t last;
    uint32_t last_ms;
    int32_t vx, vy;   /* EMA px/s */
    bool active;
} gesture_ctx_t;

static void gesture_cb(lv_event_t *e)
{
    gesture_ctx_t *c = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (code == LV_EVENT_DELETE) {
        lv_free(c);
        return;
    }
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    uint32_t now = lv_tick_get();

    if (code == LV_EVENT_PRESSED) {
        c->active = true;
        c->g = (ui_gesture_info_t){.start = p};
        c->last = p;
        c->last_ms = now;
        c->vx = c->vy = 0;
        if (c->cbs.on_press) c->cbs.on_press(&c->g, c->cbs.user);
    } else if (code == LV_EVENT_PRESSING && c->active) {
        uint32_t dt = now - c->last_ms;
        if (dt > 0) {
            int32_t ivx = (p.x - c->last.x) * 1000 / (int32_t)dt;
            int32_t ivy = (p.y - c->last.y) * 1000 / (int32_t)dt;
            c->vx = (c->vx + ivx) / 2;
            c->vy = (c->vy + ivy) / 2;
        }
        c->last = p;
        c->last_ms = now;
        c->g.dx = p.x - c->g.start.x;
        c->g.dy = p.y - c->g.start.y;
        if (c->g.axis == UI_AXIS_NONE) {
            if (LV_ABS(c->g.dx) >= UI_AXIS_LOCK || LV_ABS(c->g.dy) >= UI_AXIS_LOCK) {
                c->g.axis = LV_ABS(c->g.dx) > LV_ABS(c->g.dy) ? UI_AXIS_H : UI_AXIS_V;
            }
        }
        if (c->g.axis != UI_AXIS_NONE && c->cbs.on_drag) c->cbs.on_drag(&c->g, c->cbs.user);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && c->active) {
        c->active = false;
        c->g.vx = c->vx;
        c->g.vy = c->vy;
        c->g.dir = UI_SWIPE_NONE;
        if (code == LV_EVENT_RELEASED) {
            if (c->g.axis == UI_AXIS_H &&
                (LV_ABS(c->g.dx) >= UI_SWIPE_MIN || LV_ABS(c->g.vx) >= UI_SWIPE_VEL)) {
                c->g.dir = (c->g.dx < 0) ? UI_SWIPE_LEFT : UI_SWIPE_RIGHT;
            } else if (c->g.axis == UI_AXIS_V &&
                       (LV_ABS(c->g.dy) >= UI_SWIPE_MIN || LV_ABS(c->g.vy) >= UI_SWIPE_VEL)) {
                c->g.dir = (c->g.dy < 0) ? UI_SWIPE_UP : UI_SWIPE_DOWN;
            }
        }
        if (c->cbs.on_release) c->cbs.on_release(&c->g, c->cbs.user);
    }
}

void ui_gesture_attach(lv_obj_t *obj, const ui_gesture_cbs_t *cbs)
{
    gesture_ctx_t *c = lv_malloc_zeroed(sizeof(*c));
    LV_ASSERT_MALLOC(c);
    c->cbs = *cbs;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, gesture_cb, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(obj, gesture_cb, LV_EVENT_PRESSING, c);
    lv_obj_add_event_cb(obj, gesture_cb, LV_EVENT_RELEASED, c);
    lv_obj_add_event_cb(obj, gesture_cb, LV_EVENT_PRESS_LOST, c);
    lv_obj_add_event_cb(obj, gesture_cb, LV_EVENT_DELETE, c);
}

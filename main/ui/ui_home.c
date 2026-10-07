/**
 * Home screen: minimal, immersive. Only the current bot's full-screen avatar, its name and
 * its relay status line, overlaid on the lower part of the avatar over a soft dark
 * gradient. Time, Wi-Fi, battery, hints and the conversation toggle all live behind
 * gestures (swipe down = device settings, swipe up = bot panel). A conversation being
 * open shows only as the avatar's rim glow.
 *
 * Carousel = 3 full-screen slots (prev / current / next). Bot for slot k is (sel + k) mod N,
 * so it is an infinite circle with no end stop or bounce. Neighbours are hidden at rest;
 * while dragging, the current avatar slides + fades + shrinks out and the next slides in.
 * On release we animate one full step, rotate the slots and fade the new name in.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#define NSLOT UI_CAROUSEL_SLOTS
#define HALF  (NSLOT / 2)

static lv_obj_t    *s_caption;          /* name + status, fades during swipes */
static lv_obj_t    *s_name, *s_name_sh;
static lv_obj_t    *s_status, *s_status_sh;
static lv_obj_t    *s_grab_top, *s_grab_bottom;
static ui_avatar_t *s_slot[NSLOT];
static int          s_slot_bot[NSLOT];
static int32_t      s_offset;
static int          s_pending_step;     /* +1 next, -1 prev, 0 snap back */
static bool         s_animating;
static lv_timer_t  *s_level_timer;

static int wrap(int i)
{
    int n = g_ui.bot_count > 0 ? g_ui.bot_count : 1;
    return ((i % n) + n) % n;
}

static bool turn_locked(void)
{
    /* Don't change bots mid-turn: the reply belongs to the bot it was sent to. */
    return g_ui.turn_active || g_ui.state == MUSE_STATUS_LISTENING ||
           g_ui.state == MUSE_STATUS_UPLOADING || g_ui.state == MUSE_STATUS_SPEAKING;
}

/* ------------------------------------------------------------------ layout */

static void layout(int32_t offset)
{
    s_offset = offset;
    for (int k = -HALF; k <= HALF; k++) {
        ui_avatar_t *av = s_slot[k + HALF];
        lv_obj_t *o = ui_avatar_obj(av);
        int32_t x = k * UI_CAROUSEL_SPACING + offset;
        int32_t t = LV_MIN(LV_ABS(x), UI_CAROUSEL_SPACING) * 256 / UI_CAROUSEL_SPACING;   /* 0..256 */
        int32_t opa = 255 - (255 * t) / 256;
        if (k != 0 && opa < 8) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);      /* invisible at rest: not drawn at all */
            continue;
        }
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(o, x);
        lv_obj_set_style_opa(o, (lv_opa_t)opa, 0);
        ui_avatar_set_base_scale(av, 256 - ((256 - UI_NEIGHBOR_SCALE) * t) / 256);
    }
    int32_t half = UI_CAROUSEL_SPACING / 3;
    int32_t f = LV_MIN(LV_ABS(offset), half) * 255 / half;
    lv_obj_set_style_opa(s_caption, (lv_opa_t)(255 - f), 0);
}

static void set_text2(lv_obj_t *a, lv_obj_t *shadow, const char *txt)
{
    lv_label_set_text(a, txt);
    lv_label_set_text(shadow, txt);
}

static void bind_slots(void)
{
    if (g_ui.bot_count == 0) return;
    for (int k = -HALF; k <= HALF; k++) {
        int bi = wrap(g_ui.sel + k);
        ui_avatar_t *av = s_slot[k + HALF];
        if (s_slot_bot[k + HALF] != bi) {
            ui_avatar_set_bot(av, &g_ui.bots[bi]);
            s_slot_bot[k + HALF] = bi;
        }
        ui_avatar_set_anim(av, k == 0 ? ui_avatar_anim_for_status(g_ui.state) : AVATAR_ANIM_STATIC);
        ui_avatar_set_glow(av, k == 0 && g_ui.conv_active);
    }
    set_text2(s_name, s_name_sh, g_ui.bots[g_ui.sel].name);
}

/* ------------------------------------------------------------------ swipe anim */

static void offset_exec(void *var, int32_t v)
{
    (void)var;
    layout(v);
}

static void caption_fade_exec(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_in_caption(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_caption);
    lv_anim_set_exec_cb(&a, caption_fade_exec);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void step_done(lv_anim_t *a)
{
    (void)a;
    s_animating = false;
    if (s_pending_step) {
        g_ui.sel = wrap(g_ui.sel + s_pending_step);
        /* Rotate slot→bot cache so only the newly exposed slot is rebound. */
        if (s_pending_step > 0) {
            ui_avatar_t *first = s_slot[0];
            int fb = s_slot_bot[0];
            memmove(&s_slot[0], &s_slot[1], sizeof(s_slot[0]) * (NSLOT - 1));
            memmove(&s_slot_bot[0], &s_slot_bot[1], sizeof(s_slot_bot[0]) * (NSLOT - 1));
            s_slot[NSLOT - 1] = first;
            s_slot_bot[NSLOT - 1] = fb;
        } else {
            ui_avatar_t *last = s_slot[NSLOT - 1];
            int lb = s_slot_bot[NSLOT - 1];
            memmove(&s_slot[1], &s_slot[0], sizeof(s_slot[0]) * (NSLOT - 1));
            memmove(&s_slot_bot[1], &s_slot_bot[0], sizeof(s_slot_bot[0]) * (NSLOT - 1));
            s_slot[0] = last;
            s_slot_bot[0] = lb;
        }
        bind_slots();
        layout(0);
        lv_obj_set_style_opa(s_caption, LV_OPA_TRANSP, 0);
        fade_in_caption();   /* brief name fade-in after switching */
        ui_post(APP_EV_BOT_SELECTED, g_ui.sel, 0, g_ui.bots[g_ui.sel].id);
    } else {
        layout(0);
    }
    s_pending_step = 0;
}

static void animate_to(int32_t target, int step)
{
    s_pending_step = step;
    s_animating = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_offset);
    lv_anim_set_exec_cb(&a, offset_exec);
    lv_anim_set_values(&a, s_offset, target);
    uint32_t dist = (uint32_t)LV_ABS(target - s_offset);
    lv_anim_set_duration(&a, 140 + dist * 180 / UI_CAROUSEL_SPACING);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, step_done);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ gestures */

static void show_grabbers(const ui_gesture_info_t *g, bool on)
{
#if CONFIG_MUSE_UI_EDGE_HINTS
    bool top = on && g && g->start.y <= UI_EDGE_TOP;
    bool bottom = on && g && g->start.y >= UI_EDGE_BOTTOM;
    if (top) lv_obj_remove_flag(s_grab_top, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_grab_top, LV_OBJ_FLAG_HIDDEN);
    if (bottom) lv_obj_remove_flag(s_grab_bottom, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_grab_bottom, LV_OBJ_FLAG_HIDDEN);
#else
    (void)g;
    (void)on;
#endif
}

static void on_press(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g_ui.panel == UI_PANEL_NONE && !g_ui.locked) show_grabbers(g, true);
}

static void on_drag(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g_ui.panel != UI_PANEL_NONE || g_ui.locked || s_animating) return;
    if (g->axis != UI_AXIS_H || g_ui.bot_count < 2 || turn_locked()) return;
    int32_t lim = UI_CAROUSEL_SPACING;
    layout(LV_CLAMP(-lim, g->dx, lim));
}

static void on_release(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    show_grabbers(g, false);
    ui_note_activity();
    if (g_ui.panel != UI_PANEL_NONE || g_ui.locked) return;   /* panels own their input */
    if (g->axis == UI_AXIS_H) {
        if (s_animating) return;
        if (g_ui.bot_count < 2 || turn_locked()) {
            animate_to(0, 0);
        } else if (g->dir == UI_SWIPE_LEFT) {
            animate_to(-UI_CAROUSEL_SPACING, +1);    /* next bot (wraps last → first) */
        } else if (g->dir == UI_SWIPE_RIGHT) {
            animate_to(UI_CAROUSEL_SPACING, -1);     /* previous bot (wraps first → last) */
        } else {
            animate_to(0, 0);
        }
        return;
    }
    if (g->dir == UI_SWIPE_UP && g->start.y >= UI_EDGE_BOTTOM) {
        ui_bot_panel_open();
    } else if (g->dir == UI_SWIPE_DOWN && g->start.y <= UI_EDGE_TOP) {
        ui_device_panel_open();
    }
}

static void level_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (g_ui.level_fn && g_ui.bot_count) ui_avatar_set_level(s_slot[HALF], g_ui.level_fn());
}

/* ------------------------------------------------------------------ build / refresh */

static lv_obj_t *caption_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col,
                               int32_t y, int32_t dx, int32_t dy, int32_t width)
{
    lv_obj_t *l = ui_label(parent, font, col, "");
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, dx, y + dy);
    return l;
}

static lv_obj_t *edge_grabber(lv_obj_t *parent, int32_t y)
{
    lv_obj_t *g = ui_plain(parent);
    lv_obj_set_size(g, 36, 4);
    lv_obj_set_style_radius(g, 2, 0);
    lv_obj_set_style_bg_color(g, UI_COL_TEXT, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_20, 0);   /* very faint, only while touching */
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    return g;
}

void ui_home_build(lv_obj_t *scr)
{
    lv_obj_t *track = ui_plain(scr);
    lv_obj_set_size(track, UI_W, UI_H);
    for (int i = 0; i < NSLOT; i++) {
        s_slot[i] = ui_avatar_create(track, &ui_avatar_renderer_procedural);
        lv_obj_set_y(ui_avatar_obj(s_slot[i]), UI_AVATAR_DY);
        s_slot_bot[i] = -1;
    }

    /* Soft dark gradient over the lower avatar so white text stays legible on any color
     * (yellow Photon, cream bodies). Not a panel: transparent at the top, no edges. */
    static lv_grad_dsc_t grad;   /* style keeps a pointer: must outlive the object */
    static const lv_opa_t opas[3] = {LV_OPA_TRANSP, UI_SCRIM_MID_OPA, UI_SCRIM_END_OPA};
    static const uint8_t fracs[3] = {0, UI_SCRIM_MID_FRAC, 255};
    lv_color_t cols[3] = {UI_COL_BG, UI_COL_BG, UI_COL_BG};
    lv_grad_init_stops(&grad, cols, opas, fracs, 3);
    lv_grad_vertical_init(&grad);
    lv_obj_t *scrim = ui_plain(scr);
    lv_obj_set_size(scrim, UI_W, UI_H - UI_SCRIM_Y);
    lv_obj_set_pos(scrim, 0, UI_SCRIM_Y);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(scrim, &grad, 0);

    s_caption = ui_plain(scr);
    lv_obj_set_size(s_caption, UI_W, UI_H);
    /* Cheap text shadow: a dark copy 2 px down-right under each label. */
    s_name_sh = caption_label(s_caption, UI_FONT_NAME, UI_COL_BG, UI_NAME_Y, 2, 2, 340);
    lv_obj_set_style_text_opa(s_name_sh, LV_OPA_70, 0);
    s_name = caption_label(s_caption, UI_FONT_NAME, UI_COL_TEXT, UI_NAME_Y, 0, 0, 340);
    s_status_sh = caption_label(s_caption, UI_FONT_BODY, UI_COL_BG, UI_STATUS_Y, 1, 2, UI_STATUS_W);
    lv_obj_set_style_text_opa(s_status_sh, LV_OPA_70, 0);
    s_status = caption_label(s_caption, UI_FONT_BODY, lv_color_hex(0xE5E7EB), UI_STATUS_Y, 0, 0, UI_STATUS_W);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(s_status_sh, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_status, 44);      /* up to two lines, then "..." */
    lv_obj_set_height(s_status_sh, 44);

    s_grab_top = edge_grabber(scr, 10);
    s_grab_bottom = edge_grabber(scr, UI_H - 14);

    ui_gesture_cbs_t cbs = {.on_press = on_press, .on_drag = on_drag, .on_release = on_release};
    ui_gesture_attach(scr, &cbs);
    s_level_timer = lv_timer_create(level_poll_cb, 50, NULL);
    lv_timer_pause(s_level_timer);
    layout(0);
}

void ui_home_refresh_bots(void)
{
    for (int i = 0; i < NSLOT; i++) s_slot_bot[i] = -1;   /* force rebind */
    bind_slots();
    layout(0);
}

void ui_home_refresh_status(void)
{
    if (!s_status) return;
    set_text2(s_status, s_status_sh, g_ui.status_text[0] ? g_ui.status_text : "Ready");
    lv_obj_set_style_text_color(s_status, g_ui.state == MUSE_STATUS_ERROR ? lv_color_hex(0xFCA5A5)
                                                                          : lv_color_hex(0xE5E7EB), 0);
    if (g_ui.bot_count) {
        ui_avatar_set_anim(s_slot[HALF], ui_avatar_anim_for_status(g_ui.state));
        ui_avatar_set_glow(s_slot[HALF], g_ui.conv_active);
    }
    if (g_ui.state == MUSE_STATUS_SPEAKING) {
        lv_timer_resume(s_level_timer);
    } else {
        lv_timer_pause(s_level_timer);
    }
}

void ui_home_set_level(uint8_t level)
{
    if (g_ui.bot_count) ui_avatar_set_level(s_slot[HALF], level);
}

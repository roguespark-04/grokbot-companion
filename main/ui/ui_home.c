/**
 * Home screen: infinite bot carousel + status line + status bar.
 *
 * Carousel = 5 avatar slots at offsets -2..+2 around the selected index. Bot for slot k
 * is (sel + k) mod N, so swiping past the last bot slides straight into the first with
 * no end stop or bounce — the ring wraps in both directions. While dragging, slots follow
 * the finger; on release we animate one full step and rebind the slots.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>

#define NSLOT UI_CAROUSEL_SLOTS
#define HALF  (NSLOT / 2)

static lv_obj_t    *s_time;
static lv_obj_t    *s_icons;
static lv_obj_t    *s_name;
static lv_obj_t    *s_status_dot;
static lv_obj_t    *s_status;
static lv_obj_t    *s_hint;
static lv_obj_t    *s_dots;
static ui_avatar_t *s_slot[NSLOT];
static int          s_slot_bot[NSLOT];
static int32_t      s_offset;
static int          s_pending_step;   /* +1 next, -1 prev, 0 snap back */
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
        int32_t x = UI_CX + k * UI_CAROUSEL_SPACING + offset;
        int32_t dist = LV_ABS(x - UI_CX);
        int32_t t = LV_MIN(dist, UI_CAROUSEL_SPACING) * 256 / UI_CAROUSEL_SPACING;
        int32_t scale = 256 - ((256 - UI_PEEK_SCALE) * t) / 256;
        int32_t opa = 255 - ((255 - UI_PEEK_OPA) * t) / 256;
        lv_obj_t *o = ui_avatar_obj(av);
        lv_obj_set_pos(o, x - UI_AVATAR_BOX / 2, UI_CAROUSEL_Y - UI_AVATAR_BOX / 2);
        lv_obj_set_style_opa(o, (lv_opa_t)opa, 0);
        ui_avatar_set_base_scale(av, scale);
    }
    /* name fades while between bots */
    int32_t f = LV_MIN(LV_ABS(offset), UI_CAROUSEL_SPACING / 2) * 255 / (UI_CAROUSEL_SPACING / 2);
    lv_obj_set_style_opa(s_name, (lv_opa_t)(255 - f), 0);
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
    }
    lv_label_set_text(s_name, g_ui.bots[g_ui.sel].name);

    /* page dots */
    uint32_t n = lv_obj_get_child_count(s_dots);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *d = lv_obj_get_child(s_dots, (int32_t)i);
        bool on = (int)i == g_ui.sel;
        if ((int)i >= g_ui.bot_count) {
            lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(d, on ? 16 : 6, 6);
        lv_obj_set_style_bg_color(d, on ? lv_color_hex(g_ui.bots[i].color) : UI_COL_DIM, 0);
    }
}

/* ------------------------------------------------------------------ swipe anim */

static void offset_exec(void *var, int32_t v)
{
    (void)var;
    layout(v);
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
    lv_anim_set_duration(&a, 120 + dist * 160 / UI_CAROUSEL_SPACING);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, step_done);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ gestures */

static void on_drag(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g_ui.panel != UI_PANEL_NONE || g_ui.locked || s_animating) return;
    if (g->axis != UI_AXIS_H || g_ui.bot_count < 2 || turn_locked()) return;
    int32_t lim = UI_CAROUSEL_SPACING + UI_CAROUSEL_SPACING / 4;
    layout(LV_CLAMP(-lim, g->dx, lim));
}

static void on_release(const ui_gesture_info_t *g, void *user)
{
    (void)user;
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

static lv_obj_t *grabber(lv_obj_t *parent, int32_t y)
{
    lv_obj_t *g = ui_plain(parent);
    lv_obj_set_size(g, 40, 5);
    lv_obj_set_style_radius(g, 3, 0);
    lv_obj_set_style_bg_color(g, UI_COL_DIM, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, y);
    return g;
}

void ui_home_build(lv_obj_t *scr)
{
    grabber(scr, 10);
    s_time = ui_label(scr, UI_FONT_TITLE, UI_COL_TEXT, "--:--");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 24);
    s_icons = ui_label(scr, UI_FONT_SMALL, UI_COL_MUTED, "");
    lv_obj_align(s_icons, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *track = ui_plain(scr);
    lv_obj_set_size(track, UI_W, UI_H);
    for (int i = 0; i < NSLOT; i++) {
        s_slot[i] = ui_avatar_create(track, &ui_avatar_renderer_procedural);
        s_slot_bot[i] = -1;
    }

    s_name = ui_label(scr, UI_FONT_NAME, UI_COL_TEXT, "");
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 290);

    lv_obj_t *row = ui_plain(scr);
    lv_obj_set_size(row, 330, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 334);
    s_status_dot = ui_plain(row);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    s_status = ui_label(row, UI_FONT_BODY, UI_COL_TEXT, "Ready");
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_max_width(s_status, 290, 0);

    s_hint = ui_label(scr, UI_FONT_SMALL, UI_COL_MUTED, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 366);

    s_dots = ui_plain(scr);
    lv_obj_set_size(s_dots, 300, 10);
    lv_obj_set_flex_flow(s_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dots, 6, 0);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 400);
    for (int i = 0; i < BOT_MAX; i++) {
        lv_obj_t *d = ui_plain(s_dots);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    grabber(scr, UI_H - 18);

    ui_gesture_cbs_t cbs = {.on_drag = on_drag, .on_release = on_release};
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
    lv_label_set_text(s_status, g_ui.status_text[0] ? g_ui.status_text : "Ready");
    lv_obj_set_style_bg_color(s_status_dot, ui_status_color(g_ui.state), 0);
    if (g_ui.bot_count) {
        ui_avatar_set_anim(s_slot[HALF], ui_avatar_anim_for_status(g_ui.state));
    }
    if (g_ui.state == MUSE_STATUS_SPEAKING) {
        lv_timer_resume(s_level_timer);
    } else {
        lv_timer_pause(s_level_timer);
    }
    const char *hint;
    if (g_ui.settings.talk_mode == TALK_MODE_ALWAYS) {
        hint = g_ui.conv_active ? "Always listening - just talk" : "Always-listen - swipe up to start";
    } else {
        hint = g_ui.conv_active ? "Conversation open - hold BOOT" : "Hold BOOT to talk";
    }
    lv_label_set_text(s_hint, hint);
}

void ui_home_refresh_statusbar(void)
{
    char t[24], icons[64];
    ui_format_time(t, sizeof(t), false);
    lv_label_set_text(s_time, t);
    const char *wifi = g_ui.dev.wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
    if (g_ui.dev.battery_pct >= 0) {
        snprintf(icons, sizeof(icons), "%s   %s %d%%", wifi,
                 ui_battery_symbol(g_ui.dev.battery_pct, g_ui.dev.charging), g_ui.dev.battery_pct);
    } else {
        snprintf(icons, sizeof(icons), "%s   %s", wifi, ui_battery_symbol(-1, g_ui.dev.charging));
    }
    lv_label_set_text(s_icons, icons);
}

void ui_home_set_level(uint8_t level)
{
    if (g_ui.bot_count) ui_avatar_set_level(s_slot[HALF], level);
}

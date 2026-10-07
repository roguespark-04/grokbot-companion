/**
 * Per-bot panel — swipe UP from the bottom edge of the home screen.
 *
 *   Talk mode     Press to talk (DEFAULT) | Always listen
 *   Conversation  Start / End
 *   Voice         <current voice>  ›   → voice picker (per-bot, NVS voice_<bot_id>)
 *   Volume        slider
 *   Brightness    slider (live)
 *   Auto-sleep    15s | 30s | 1m | 5m | Never
 *
 * Close: swipe down on (or tap) the top grabber. Horizontal swipes in here never change
 * the bot: the panel sits on lv_layer_top() and covers the home screen, its content only
 * scrolls vertically, and the home gesture handler also checks g_ui.panel.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_panel;
static lv_obj_t *s_title;
static lv_obj_t *s_title_dot;
static lv_obj_t *s_mode;
static lv_obj_t *s_conv_btn;
static lv_obj_t *s_conv_lbl;
static lv_obj_t *s_voice_val;
static lv_obj_t *s_vol;
static lv_obj_t *s_vol_lbl;
static lv_obj_t *s_bri;
static lv_obj_t *s_bri_lbl;
static lv_obj_t *s_sleep;

static const char *MODE_MAP[] = {"Press to talk", "Always listen", ""};
static const char *SLEEP_MAP[] = {"15s", "30s", "1m", "5m", "Never", ""};
static const uint16_t SLEEP_VALUES[] = {15, 30, 60, 300, 0};

/* ------------------------------------------------------------------ widgets */

static void style_card(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_pad_all(o, 12, 0);
    lv_obj_set_style_border_width(o, 0, 0);
}

static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = ui_plain(parent);
    style_card(c);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 8, 0);
    return c;
}

static lv_obj_t *seg(lv_obj_t *parent, const char **map)
{
    lv_obj_t *m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_obj_set_size(m, lv_pct(100), 46);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_column(m, 6, 0);
    lv_obj_set_style_text_font(m, UI_FONT_SMALL, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, UI_COL_SURFACE2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, UI_COL_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, UI_COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, lv_color_hex(0x0B1220), LV_PART_ITEMS | LV_STATE_CHECKED);
    return m;
}

static lv_obj_t *slider(lv_obj_t *parent, int32_t min, int32_t max)
{
    lv_obj_t *s = lv_slider_create(parent);
    lv_slider_set_range(s, min, max);
    lv_obj_set_width(s, lv_pct(96));
    lv_obj_set_height(s, 10);
    lv_obj_set_style_bg_color(s, UI_COL_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, UI_COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_set_style_align(s, LV_ALIGN_CENTER, 0);
    return s;
}

/* ------------------------------------------------------------------ events */

static void mode_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target_obj(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(m);
    if (id > 1) return;
    g_ui.settings.talk_mode = (talk_mode_t)id;
    ui_home_refresh_status();
    ui_post(APP_EV_TALK_MODE, (int32_t)id, 0, NULL);
}

static void conv_cb(lv_event_t *e)
{
    (void)e;
    ui_post(APP_EV_CONVERSATION_TOGGLE, 0, 0, NULL);
}

static void voice_row_cb(lv_event_t *e)
{
    (void)e;
    ui_voice_picker_open();
}

static void vol_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int32_t v = lv_slider_get_value(s_vol);
    lv_label_set_text_fmt(s_vol_lbl, LV_SYMBOL_VOLUME_MAX "  Volume  %d%%", (int)v);
    ui_post(APP_EV_VOLUME, v, code == LV_EVENT_RELEASED, NULL);
}

static void bri_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int32_t v = lv_slider_get_value(s_bri);
    lv_label_set_text_fmt(s_bri_lbl, LV_SYMBOL_EYE_OPEN "  Brightness  %d%%", (int)v);
    ui_set_brightness((uint8_t)v);   /* live preview */
    if (code == LV_EVENT_RELEASED) ui_post(APP_EV_BRIGHTNESS, v, 1, NULL);
}

static void sleep_cb(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id >= sizeof(SLEEP_VALUES) / sizeof(SLEEP_VALUES[0])) return;
    g_ui.settings.sleep_s = SLEEP_VALUES[id];
    ui_post(APP_EV_SLEEP_TIMEOUT, SLEEP_VALUES[id], 0, NULL);
}

static void panel_activity_cb(lv_event_t *e)
{
    (void)e;
    ui_note_activity();
}

static void grab_release(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g->dir == UI_SWIPE_DOWN || (g->axis == UI_AXIS_NONE && g->dir == UI_SWIPE_NONE)) {
        ui_bot_panel_close();
    }
}

/* ------------------------------------------------------------------ build */

void ui_bot_panel_build(void)
{
    s_panel = ui_plain(lv_layer_top());
    lv_obj_set_size(s_panel, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_panel, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);   /* swallow input */
    lv_obj_add_event_cb(s_panel, panel_activity_cb, LV_EVENT_PRESSED, NULL);

    /* grab zone: swipe down / tap closes */
    lv_obj_t *grab = ui_plain(s_panel);
    lv_obj_set_size(grab, UI_W, 84);
    lv_obj_align(grab, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *bar = ui_plain(grab);
    lv_obj_set_size(bar, 40, 5);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *trow = ui_plain(grab);
    lv_obj_set_size(trow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(trow, 8, 0);
    lv_obj_align(trow, LV_ALIGN_TOP_MID, 0, 34);
    s_title_dot = ui_plain(trow);
    lv_obj_set_size(s_title_dot, 14, 14);
    lv_obj_set_style_radius(s_title_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_title_dot, LV_OPA_COVER, 0);
    s_title = ui_label(trow, UI_FONT_TITLE, UI_COL_TEXT, "");
    ui_gesture_cbs_t gcb = {.on_release = grab_release};
    ui_gesture_attach(grab, &gcb);

    /* vertical-only scrolling content */
    lv_obj_t *content = ui_plain(s_panel);
    lv_obj_set_size(content, 340, UI_H - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_style_pad_bottom(content, 110, 0);   /* round bottom edge */

    lv_obj_t *c = card(content);
    ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, "Talk mode");
    s_mode = seg(c, MODE_MAP);
    lv_obj_add_event_cb(s_mode, mode_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_conv_btn = lv_button_create(content);
    lv_obj_set_size(s_conv_btn, lv_pct(100), 52);
    lv_obj_set_style_radius(s_conv_btn, 26, 0);
    lv_obj_set_style_shadow_width(s_conv_btn, 0, 0);
    s_conv_lbl = ui_label(s_conv_btn, UI_FONT_BODY, lv_color_hex(0x07130B), "");
    lv_obj_center(s_conv_lbl);
    lv_obj_add_event_cb(s_conv_btn, conv_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vrow = lv_button_create(content);
    style_card(vrow);
    lv_obj_set_style_shadow_width(vrow, 0, 0);
    lv_obj_set_size(vrow, lv_pct(100), 56);
    ui_label(vrow, UI_FONT_BODY, UI_COL_TEXT, LV_SYMBOL_AUDIO "  Voice");
    lv_obj_align(lv_obj_get_child(vrow, 0), LV_ALIGN_LEFT_MID, 0, 0);
    s_voice_val = ui_label(vrow, UI_FONT_SMALL, UI_COL_MUTED, "");
    lv_obj_set_style_max_width(s_voice_val, 170, 0);
    lv_label_set_long_mode(s_voice_val, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_voice_val, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(vrow, voice_row_cb, LV_EVENT_CLICKED, NULL);

    c = card(content);
    s_vol_lbl = ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, "");
    s_vol = slider(c, 0, 100);
    lv_obj_add_event_cb(s_vol, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_vol, vol_cb, LV_EVENT_RELEASED, NULL);

    c = card(content);
    s_bri_lbl = ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, "");
    s_bri = slider(c, 5, 100);
    lv_obj_add_event_cb(s_bri, bri_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bri, bri_cb, LV_EVENT_RELEASED, NULL);

    c = card(content);
    ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, LV_SYMBOL_POWER "  Auto-sleep");
    s_sleep = seg(c, SLEEP_MAP);
    lv_obj_set_height(s_sleep, 40);
    lv_obj_add_event_cb(s_sleep, sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ------------------------------------------------------------------ refresh / open */

void ui_bot_panel_refresh(void)
{
    if (!s_panel) return;
    if (g_ui.bot_count) {
        const bot_info_t *b = &g_ui.bots[g_ui.sel];
        lv_label_set_text(s_title, b->name);
        lv_obj_set_style_bg_color(s_title_dot, lv_color_hex(b->color), 0);
    }
    lv_buttonmatrix_set_button_ctrl(s_mode, g_ui.settings.talk_mode == TALK_MODE_ALWAYS ? 1 : 0,
                                    LV_BUTTONMATRIX_CTRL_CHECKED);
    bool on = g_ui.conv_active;
    lv_label_set_text(s_conv_lbl, on ? LV_SYMBOL_STOP "  End conversation"
                                     : LV_SYMBOL_PLAY "  Start conversation");
    lv_obj_set_style_bg_color(s_conv_btn, on ? UI_COL_ERR : UI_COL_OK, 0);

    lv_label_set_text_fmt(s_voice_val, "%s " LV_SYMBOL_RIGHT, ui_voice_name(g_ui.cur_voice));

    lv_slider_set_value(s_vol, g_ui.settings.volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_vol_lbl, LV_SYMBOL_VOLUME_MAX "  Volume  %d%%", g_ui.settings.volume);
    lv_slider_set_value(s_bri, g_ui.settings.brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_bri_lbl, LV_SYMBOL_EYE_OPEN "  Brightness  %d%%", g_ui.settings.brightness);
    for (uint32_t i = 0; i < sizeof(SLEEP_VALUES) / sizeof(SLEEP_VALUES[0]); i++) {
        if (SLEEP_VALUES[i] == g_ui.settings.sleep_s) {
            lv_buttonmatrix_set_button_ctrl(s_sleep, i, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
    }
}

void ui_bot_panel_open(void)
{
    if (g_ui.panel != UI_PANEL_NONE) return;
    ui_bot_panel_refresh();
    ui_set_panel(UI_PANEL_BOT);
    ui_panel_slide(s_panel, true, UI_H);
}

void ui_bot_panel_close(void)
{
    if (g_ui.panel == UI_PANEL_VOICE_PICKER) ui_voice_picker_close();
    ui_set_panel(UI_PANEL_NONE);
    ui_panel_slide(s_panel, false, UI_H);
}

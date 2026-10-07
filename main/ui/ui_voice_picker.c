/**
 * Voice picker — opened from the bot panel's Voice row.
 *
 * Scrollable roller of the Grok Bot app's voices from GET /voices (relay/voices.json:
 * 28 xAI grok-tts ids, VOICE_MAX = 32). The device stores the choice per bot and sends
 * that voice_id with target_bot_id on POST /upload; the reply side synthesizes with it.
 * Preview plays GET /voices/<id>/sample.wav and is only enabled when the relay lists a
 * sample for that voice (optional, TODO: clips generated with xAI TTS).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_root;
static lv_obj_t *s_title;
static lv_obj_t *s_roller;
static lv_obj_t *s_desc;
static lv_obj_t *s_note;
static lv_obj_t *s_preview;
static char      s_opts[VOICE_MAX * (VOICE_NAME_MAX + 1)];

static int roller_voice_idx(void)
{
    if (g_ui.voice_count == 0) return -1;
    int i = (int)lv_roller_get_selected(s_roller);
    return (i >= 0 && i < g_ui.voice_count) ? i : -1;
}

static void update_desc(void)
{
    int i = roller_voice_idx();
    const char *d = i >= 0 ? g_ui.voices[i].description : "No voices from the relay yet.";
    lv_label_set_text(s_desc, d[0] ? d : " ");
    if (i >= 0 && g_ui.voices[i].has_sample) {
        lv_obj_remove_state(s_preview, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_preview, LV_STATE_DISABLED);   /* no clip yet: TODO via xAI TTS */
    }
}

static void roller_cb(lv_event_t *e)
{
    (void)e;
    update_desc();
}

static void preview_cb(lv_event_t *e)
{
    (void)e;
    int i = roller_voice_idx();
    if (i >= 0) ui_post(APP_EV_VOICE_PREVIEW, 0, 0, g_ui.voices[i].id);
}

static void select_cb(lv_event_t *e)
{
    (void)e;
    int i = roller_voice_idx();
    if (i >= 0) {
        strlcpy(g_ui.cur_voice, g_ui.voices[i].id, sizeof(g_ui.cur_voice));
        ui_post(APP_EV_VOICE_SELECTED, 0, 0, g_ui.voices[i].id);
        ui_bot_panel_refresh();
    }
    ui_voice_picker_close();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_voice_picker_close();
}

static void grab_release(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g->dir == UI_SWIPE_DOWN) ui_voice_picker_close();
}

static lv_obj_t *pill_button(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg,
                             lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 140, 48);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = ui_label(b, UI_FONT_BODY, fg, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

void ui_voice_picker_build(void)
{
    s_root = ui_plain(lv_layer_top());
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_root, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *grab = ui_plain(s_root);
    lv_obj_set_size(grab, UI_W, 80);
    lv_obj_t *bar = ui_plain(grab);
    lv_obj_set_size(bar, 40, 5);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 12);
    s_title = ui_label(grab, UI_FONT_TITLE, UI_COL_TEXT, "Voice");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 36);
    ui_gesture_cbs_t gcb = {.on_release = grab_release};
    ui_gesture_attach(grab, &gcb);

    s_roller = lv_roller_create(s_root);
    lv_roller_set_visible_row_count(s_roller, 5);   /* 28 voices: swipe/fling to scroll */
    lv_obj_set_width(s_roller, 300);
    lv_obj_align(s_roller, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_text_font(s_roller, UI_FONT_BODY, 0);
    lv_obj_set_style_bg_color(s_roller, UI_COL_SURFACE, 0);
    lv_obj_set_style_border_width(s_roller, 0, 0);
    lv_obj_set_style_radius(s_roller, 18, 0);
    lv_obj_set_style_text_color(s_roller, UI_COL_MUTED, 0);
    lv_obj_set_style_bg_color(s_roller, UI_COL_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, lv_color_hex(0x0B1220), LV_PART_SELECTED);
    lv_obj_add_event_cb(s_roller, roller_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_desc = ui_label(s_root, UI_FONT_SMALL, UI_COL_TEXT, "");
    lv_obj_set_width(s_desc, 300);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_desc, LV_ALIGN_TOP_MID, 0, 278);

    lv_obj_t *row = ui_plain(s_root);
    lv_obj_set_size(row, 300, 52);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 316);
    s_preview = pill_button(row, LV_SYMBOL_PLAY " Preview", UI_COL_SURFACE2, UI_COL_TEXT, preview_cb);
    lv_obj_set_style_opa(s_preview, LV_OPA_40, LV_STATE_DISABLED);
    pill_button(row, LV_SYMBOL_OK " Select", UI_COL_ACCENT, lv_color_hex(0x0B1220), select_cb);

    lv_obj_t *cancel = lv_button_create(s_root);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, 120, 34);
    lv_obj_align(cancel, LV_ALIGN_TOP_MID, 0, 374);
    lv_obj_t *cl = ui_label(cancel, UI_FONT_SMALL, UI_COL_MUTED, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, NULL);

    s_note = ui_label(s_root, UI_FONT_SMALL, UI_COL_DIM, "Placeholder voices - the reply side picks the real voice");
    lv_obj_set_width(s_note, 260);
    lv_label_set_long_mode(s_note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_note, LV_ALIGN_TOP_MID, 0, 408);
}

void ui_voice_picker_refresh(void)
{
    if (!s_root) return;
    s_opts[0] = '\0';
    for (int i = 0; i < g_ui.voice_count; i++) {
        if (i) strlcat(s_opts, "\n", sizeof(s_opts));
        strlcat(s_opts, g_ui.voices[i].name, sizeof(s_opts));
    }
    lv_roller_set_options(s_roller, g_ui.voice_count ? s_opts : "(none)", LV_ROLLER_MODE_NORMAL);
    int sel = 0;
    for (int i = 0; i < g_ui.voice_count; i++) {
        if (strcmp(g_ui.voices[i].id, g_ui.cur_voice) == 0) sel = i;
    }
    lv_roller_set_selected(s_roller, (uint32_t)sel, LV_ANIM_OFF);
    if (g_ui.bot_count) lv_label_set_text_fmt(s_title, "Voice - %s", g_ui.bots[g_ui.sel].name);
    if (g_ui.voices_placeholder) {
        lv_obj_remove_flag(s_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_note, LV_OBJ_FLAG_HIDDEN);
    }
    update_desc();
}

void ui_voice_picker_open(void)
{
    if (g_ui.panel != UI_PANEL_BOT) return;
    ui_voice_picker_refresh();
    ui_set_panel(UI_PANEL_VOICE_PICKER);
    ui_panel_slide(s_root, true, UI_H);
}

void ui_voice_picker_close(void)
{
    if (g_ui.panel != UI_PANEL_VOICE_PICKER) return;
    ui_set_panel(UI_PANEL_BOT);
    ui_panel_slide(s_root, false, UI_H);
}

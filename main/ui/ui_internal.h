/**
 * @file ui_internal.h
 * @brief Shared UI state. Only touched from the LVGL task or under bsp_display_lock().
 */
#pragma once

#include "lvgl.h"
#include "ui.h"
#include "ui_avatar.h"
#include "ui_gesture.h"
#include "ui_theme.h"

typedef struct {
    app_event_cb_t cb;
    lv_display_t  *disp;
    lv_indev_t    *indev;

    bot_info_t     bots[BOT_MAX];
    int            bot_count;
    int            sel;                 /* selected bot index */

    voice_info_t   voices[VOICE_MAX];
    int            voice_count;
    bool           voices_placeholder;
    char           cur_voice[VOICE_ID_MAX];

    device_settings_t settings;
    bool           conv_active;
    bool           turn_active;
    muse_status_t  state;
    char           status_text[121];
    ui_device_info_t dev;

    ui_panel_t     panel;
    uint8_t        brightness;
    bool           locked;
    bool           dimmed;
    uint32_t       last_activity_post_ms;
    uint8_t      (*level_fn)(void);
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* core helpers (ui.c) */
void        ui_post(app_event_type_t type, int32_t ival, int32_t ival2, const char *str);
void        ui_note_activity(void);
const char *ui_status_default_text(muse_status_t s);
lv_color_t  ui_status_color(muse_status_t s);
lv_obj_t   *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt);
lv_obj_t   *ui_plain(lv_obj_t *parent);
void        ui_panel_slide(lv_obj_t *panel, bool open, int32_t from_y);
void        ui_set_panel(ui_panel_t p);
void        ui_format_time(char *buf, size_t n, bool with_date);
const char *ui_battery_symbol(int pct, bool charging);
const char *ui_voice_name(const char *voice_id);

/* home (ui_home.c) */
void ui_home_build(lv_obj_t *scr);
void ui_home_refresh_bots(void);
void ui_home_refresh_status(void);
void ui_home_set_level(uint8_t level);

/* bot panel + voice picker (ui_bot_panel.c / ui_voice_picker.c) */
void ui_bot_panel_build(void);
void ui_bot_panel_open(void);
void ui_bot_panel_close(void);
void ui_bot_panel_refresh(void);
void ui_voice_picker_build(void);
void ui_voice_picker_open(void);
void ui_voice_picker_close(void);
void ui_voice_picker_refresh(void);

/* device panel (ui_device_panel.c) */
void ui_device_panel_build(void);
void ui_device_panel_open(void);
void ui_device_panel_close(void);
void ui_device_panel_refresh(void);

/* lock overlay (ui.c) */
void ui_lock_overlay_build(void);

/**
 * Device settings — swipe DOWN from the top edge of the home screen.
 * Wi-Fi (SSID, signal, status), battery (% + charging, AXP2101), time (PCF85063/SNTP),
 * firmware version, device id, relay. Close: swipe up on (or tap) the bottom grabber.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_panel;
static lv_obj_t *s_time_big;
static lv_obj_t *s_time_sub;
static lv_obj_t *s_wifi_main;
static lv_obj_t *s_wifi_sub;
static lv_obj_t *s_bat_main;
static lv_obj_t *s_bat_bar;
static lv_obj_t *s_bat_sub;
static lv_obj_t *s_about;
static lv_timer_t *s_tick;

static lv_obj_t *card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *c = ui_plain(parent);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, title);
    return c;
}

static const char *signal_word(int8_t rssi)
{
    if (rssi == 0) return "";
    if (rssi > -55) return "Excellent";
    if (rssi > -67) return "Good";
    if (rssi > -78) return "Fair";
    return "Weak";
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    ui_device_panel_refresh();
}

static void grab_release(const ui_gesture_info_t *g, void *user)
{
    (void)user;
    if (g->dir == UI_SWIPE_UP || (g->axis == UI_AXIS_NONE && g->dir == UI_SWIPE_NONE)) {
        ui_device_panel_close();
    }
}

static void panel_activity_cb(lv_event_t *e)
{
    (void)e;
    ui_note_activity();
}

void ui_device_panel_build(void)
{
    s_panel = ui_plain(lv_layer_top());
    lv_obj_set_size(s_panel, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_panel, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_panel, panel_activity_cb, LV_EVENT_PRESSED, NULL);

    s_time_big = ui_label(s_panel, UI_FONT_NAME, UI_COL_TEXT, "--:--");
    lv_obj_align(s_time_big, LV_ALIGN_TOP_MID, 0, 34);
    s_time_sub = ui_label(s_panel, UI_FONT_SMALL, UI_COL_MUTED, "");
    lv_obj_align(s_time_sub, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *content = ui_plain(s_panel);
    lv_obj_set_size(content, 330, UI_H - 96 - 70);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_style_pad_bottom(content, 20, 0);

    lv_obj_t *c = card(content, LV_SYMBOL_WIFI "  Wi-Fi");
    s_wifi_main = ui_label(c, UI_FONT_BODY, UI_COL_TEXT, "");
    s_wifi_sub = ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, "");

    c = card(content, LV_SYMBOL_BATTERY_FULL "  Battery");
    s_bat_main = ui_label(c, UI_FONT_BODY, UI_COL_TEXT, "");
    s_bat_bar = lv_bar_create(c);
    lv_obj_set_size(s_bat_bar, lv_pct(100), 8);
    lv_bar_set_range(s_bat_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bat_bar, UI_COL_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bat_bar, UI_COL_OK, LV_PART_INDICATOR);
    s_bat_sub = ui_label(c, UI_FONT_SMALL, UI_COL_MUTED, "");

    c = card(content, LV_SYMBOL_SETTINGS "  About");
    s_about = ui_label(c, UI_FONT_SMALL, UI_COL_TEXT, "");
    lv_label_set_long_mode(s_about, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_about, lv_pct(100));

    lv_obj_t *grab = ui_plain(s_panel);
    lv_obj_set_size(grab, UI_W, 70);
    lv_obj_align(grab, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *bar = ui_plain(grab);
    lv_obj_set_size(bar, 40, 5);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -14);
    ui_gesture_cbs_t gcb = {.on_release = grab_release};
    ui_gesture_attach(grab, &gcb);

    s_tick = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_pause(s_tick);
}

void ui_device_panel_refresh(void)
{
    if (!s_panel) return;
    const ui_device_info_t *d = &g_ui.dev;
    char buf[96];

    ui_format_time(buf, sizeof(buf), false);
    lv_label_set_text(s_time_big, buf);
    if (d->time_valid) {
        char full[64];
        ui_format_time(full, sizeof(full), true);
        const char *date = strstr(full, "- ");
        lv_label_set_text_fmt(s_time_sub, "%s - %s", date ? date + strlen("- ") : "", d->time_source);
    } else {
        lv_label_set_text(s_time_sub, "Clock not set - waiting for SNTP");
    }

    lv_label_set_text(s_wifi_main, d->wifi_ssid[0] ? d->wifi_ssid : "(no network)");
    if (d->wifi_connected) {
        lv_label_set_text_fmt(s_wifi_sub, "%s - %s %d dBm - %s", d->wifi_state,
                              signal_word(d->wifi_rssi), d->wifi_rssi, d->wifi_ip);
    } else {
        lv_label_set_text(s_wifi_sub, d->wifi_state);
    }

    if (d->battery_pct >= 0) {
        lv_label_set_text_fmt(s_bat_main, "%s %d%%", ui_battery_symbol(d->battery_pct, d->charging),
                              d->battery_pct);
        lv_bar_set_value(s_bat_bar, d->battery_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bat_bar, d->battery_pct <= 15 ? UI_COL_ERR : UI_COL_OK,
                                  LV_PART_INDICATOR);
    } else {
        lv_label_set_text(s_bat_main, "Unknown");
        lv_bar_set_value(s_bat_bar, 0, LV_ANIM_OFF);
    }
    lv_label_set_text_fmt(s_bat_sub, "%s%s - %u mV",
                          d->charging ? "Charging" : (d->usb_power ? "USB power" : "On battery"),
                          "", (unsigned)d->battery_mv);

    lv_label_set_text_fmt(s_about, "Firmware  %s\nDevice ID  %s\nRelay  %s",
                          d->fw_version, d->device_id, d->relay_ok ? "reachable" : "not reachable");
}

void ui_device_panel_open(void)
{
    if (g_ui.panel != UI_PANEL_NONE) return;
    ui_device_panel_refresh();
    ui_set_panel(UI_PANEL_DEVICE);
    lv_timer_resume(s_tick);
    ui_panel_slide(s_panel, true, -UI_H);
}

void ui_device_panel_close(void)
{
    lv_timer_pause(s_tick);
    ui_set_panel(UI_PANEL_NONE);
    ui_panel_slide(s_panel, false, -UI_H);
}

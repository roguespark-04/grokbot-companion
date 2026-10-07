/**
 * @file ui.h
 * @brief Multi-bot touch UI (LVGL 9, Waveshare BSP) — public, thread-safe API.
 *
 * Screens / layers
 *   Home        : bot carousel (infinite circle, swipe ←/→ anywhere), avatar + name +
 *                 live status line, status bar (time / Wi-Fi / battery), page dots.
 *   Bot panel   : swipe UP from the bottom edge. Talk mode (press-to-talk default /
 *                 always-listen), start/end conversation, Voice row → voice picker,
 *                 volume, brightness, auto-sleep.
 *   Device panel: swipe DOWN from the top edge. Wi-Fi, battery, time, firmware, device id.
 *   Lock overlay: brief "sleeping" hint before the AMOLED goes dark.
 *
 * Every function takes the BSP LVGL lock internally — call from any task EXCEPT from
 * inside LVGL callbacks (those already hold it). UI → app traffic goes through the
 * app_event_cb_t passed to ui_init().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_events.h"
#include "bot_types.h"
#include "muse_state.h"
#include "settings_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_PANEL_NONE = 0,
    UI_PANEL_BOT,
    UI_PANEL_VOICE_PICKER,
    UI_PANEL_DEVICE,
} ui_panel_t;

typedef struct {
    /* Wi-Fi */
    char    wifi_ssid[33];
    char    wifi_state[16];     /* "Connected" / "Connecting" / ... */
    int8_t  wifi_rssi;          /* dBm, 0 = unknown */
    char    wifi_ip[16];
    bool    wifi_connected;
    /* Battery (AXP2101) */
    int     battery_pct;        /* -1 unknown */
    bool    charging;
    bool    usb_power;
    uint16_t battery_mv;
    /* Time */
    bool    time_valid;
    char    time_source[16];    /* "RTC" / "SNTP synced" / "not set" */
    /* Identity */
    char    fw_version[32];
    char    device_id[32];
    char    relay_url[64];
    bool    relay_ok;           /* last relay request succeeded */
} ui_device_info_t;

esp_err_t ui_init(app_event_cb_t cb);

/** Roster for the carousel (copied). selected_idx is shown without animation. */
void ui_set_bots(const bot_info_t *bots, int count, int selected_idx);
/** Voice list for the picker (copied). placeholder=true shows the "placeholder" note. */
void ui_set_voices(const voice_info_t *voices, int count, bool placeholder);
/** Voice chosen for the currently selected bot (voice row + picker preselect). */
void ui_set_current_voice(const char *voice_id);

/** Avatar animation + status line. text NULL → default wording for the state. */
void ui_set_state(muse_status_t state, const char *text);
/** Speaking amplitude 0..255 (smoothed in the UI). */
void ui_set_level(uint8_t level);
/** Optional: UI polls this (every 50 ms, LVGL task) while SPEAKING/LISTENING — must be
 *  non-blocking (e.g. audio_playback_level). */
void ui_set_level_provider(uint8_t (*fn)(void));

void ui_set_settings(const device_settings_t *s);
void ui_set_conversation_active(bool active);
/** While a voice turn runs the carousel is locked (the reply belongs to that bot). */
void ui_set_turn_active(bool active);
void ui_set_device_info(const ui_device_info_t *info);

/** Power/lock hooks used by power_mgr (via main.c). */
void ui_power_dim(void);
void ui_power_undim(void);
void ui_power_lock(void);
void ui_power_unlock(void);
void ui_set_brightness(uint8_t pct);

ui_panel_t ui_get_panel(void);

#ifdef __cplusplus
}
#endif

/**
 * @file app_events.h
 * @brief UI → app task events (posted from LVGL callbacks, consumed by main.c).
 */
#pragma once

#include <stdint.h>
#include "bot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_EV_BOT_SELECTED = 0,     /* ival = index, str = bot id (carousel settled) */
    APP_EV_TALK_MODE,            /* ival = talk_mode_t */
    APP_EV_CONVERSATION_TOGGLE,  /* start/end button */
    APP_EV_VOICE_SELECTED,       /* str = voice id, for the current bot */
    APP_EV_VOICE_PREVIEW,        /* str = voice id (stub) */
    APP_EV_VOLUME,               /* ival = 0..100, ival2 = 1 when final (release) */
    APP_EV_BRIGHTNESS,           /* ival = 5..100, ival2 = 1 when final */
    APP_EV_SLEEP_TIMEOUT,        /* ival = seconds, 0 = never */
    APP_EV_USER_ACTIVITY,        /* any touch (throttled) — resets auto-sleep */
    APP_EV_PANEL,                /* ival = ui_panel_t now open (0 = none) */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    int32_t ival;
    int32_t ival2;
    char str[VOICE_ID_MAX];
} app_event_t;

typedef void (*app_event_cb_t)(const app_event_t *ev);

#ifdef __cplusplus
}
#endif

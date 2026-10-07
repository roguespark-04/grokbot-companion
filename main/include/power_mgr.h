/**
 * @file power_mgr.h
 * @brief Idle sleep + lock state machine.
 *
 *   AWAKE ──(idle ≥ auto-sleep timeout)──▶ DIMMED ──(MUSE_DIM_BEFORE_SLEEP_MS)──▶ LOCKED
 *     ▲                                       │                                   │
 *     └──────── touch / BOOT / busy ──────────┘                                   │
 *     ▲                                                                           │
 *     └──── BOOT short press: wake screen only (no recording) ◀──────────────────┤
 *     └──── BOOT hold ≥ MUSE_WAKE_LONG_PRESS_MS: wake + voice turn (LISTENING) ◀─┘
 *
 * "Idle" = the app reports not busy (no upload/thinking/speaking, no always-listen
 * conversation open, current bot status not working/thinking) AND no touch/button input
 * for the timeout. Auto-sleep "Never" (0) disables it.
 *
 * LOCKED: UI hook turns the AMOLED off and disables the touch indev, Wi-Fi goes to
 * WIFI_PS_MAX_MODEM (association kept so status polls resume fast), the mic is powered
 * down, BOOT (GPIO0) is the GPIO wake source and the app task blocks so FreeRTOS tickless
 * idle can enter automatic light sleep (CONFIG_PM_ENABLE + CONFIG_FREERTOS_USE_TICKLESS_IDLE).
 *
 * Wake: the mic is re-armed and recording starts *immediately* (before the screen is back)
 * so the first words of a hold-to-talk are kept. A short press discards that audio.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_STATE_AWAKE = 0,
    PM_STATE_DIMMED,
    PM_STATE_LOCKED,
} pm_state_t;

typedef enum {
    PM_WAKE_NONE = 0,   /* nothing happened this tick */
    PM_WAKE_SHORT,      /* woke from lock by a short BOOT press — screen on, no turn */
    PM_WAKE_HOLD,       /* woke by BOOT hold — recording already running; start the turn */
} pm_wake_t;

typedef struct {
    void (*dim)(void);          /* dim + optional "sleeping" hint */
    void (*undim)(void);        /* restore brightness after DIMMED */
    void (*lock)(void);         /* screen off + touch locked */
    void (*unlock)(void);       /* screen on + touch unlocked (last bot already shown) */
} power_mgr_hooks_t;

esp_err_t  power_mgr_init(const power_mgr_hooks_t *hooks, uint16_t timeout_s);
void       power_mgr_set_timeout_s(uint16_t timeout_s);   /* 0 = never */
void       power_mgr_set_busy(bool busy);
/** Touch or button input (any task). Cancels DIMMED. */
void       power_mgr_note_activity(void);
pm_state_t power_mgr_state(void);
bool       power_mgr_is_locked(void);

/**
 * Drive the state machine from the app task loop. When it decides to lock, this call
 * BLOCKS (device in light sleep) until BOOT wakes it, then returns PM_WAKE_SHORT/HOLD.
 */
pm_wake_t  power_mgr_tick(void);

#ifdef __cplusplus
}
#endif

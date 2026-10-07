#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BOOT (GPIO0, active low) — confirmed mapping for Grok Bot Companion:
 *   awake + unlocked, press-to-talk mode : hold = capture, release = upload (as before)
 *   locked (screen off, light sleep)     : short press (< MUSE_WAKE_LONG_PRESS_MS) = wake only,
 *                                          hold = wake + start a voice turn immediately
 *   PWR = power on/off only (AXP2101 PMIC) — never PTT.
 * See power_mgr.h for the lock state machine.
 */

esp_err_t ptt_button_init(void);

/** Sample + debounce (call every ~10–20 ms from the app loop; getters also call it). */
void ptt_button_update(void);

/** Debounced level. */
bool ptt_button_is_pressed(void);
/** Raw GPIO level, no debounce (wake classification right after light sleep). */
bool ptt_button_raw_pressed(void);

/** Latched edges, cleared on read. */
bool ptt_button_was_just_pressed(void);
bool ptt_button_was_just_released(void);

/** Swallow the current press: no "just pressed" edge until it's released (wake press). */
void ptt_button_consume_until_release(void);

/** Arm BOOT as the light-sleep wake source (+ ISR that releases ptt_button_wait_wake). */
esp_err_t ptt_button_enable_wake(bool enable);
/** Block until BOOT goes low (or timeout). true = woken by BOOT. */
bool ptt_button_wait_wake(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

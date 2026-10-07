#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Press-and-hold PTT — confirmed mapping for Grok Bot Companion:
 *   BOOT (GPIO0, active low) = PTT
 *     hold    → capture audio
 *     release → stop capture and start upload→relay flow
 *   PWR = power on/off only (AXP2101 / board custom PWR). Document only;
 *     leave power management to the board/PMIC — not used for PTT.
 * Touch-screen PTT (CST9217) deferred.
 */

esp_err_t ptt_button_init(void);

/** True while PTT (BOOT) is held (debounced). */
bool ptt_button_is_pressed(void);

/** Edge helpers for the state machine (press → listen; release → upload). */
bool ptt_button_was_just_pressed(void);
bool ptt_button_was_just_released(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Press-to-talk for v1: BOOT button (GPIO0, active low).
 * PWR is on AXP2101 / TCA9554 EXIO4 — optional later.
 * Touch-screen PTT (CST9217) deferred.
 */

esp_err_t ptt_button_init(void);

/** True while PTT is held (debounced). */
bool ptt_button_is_pressed(void);

/** Edge helpers for the state machine. */
bool ptt_button_was_just_pressed(void);
bool ptt_button_was_just_released(void);

#ifdef __cplusplus
}
#endif

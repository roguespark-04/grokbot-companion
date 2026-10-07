#pragma once

#include "esp_err.h"
#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Round 466×466 AMOLED status face (CO5300 QSPI + optional LVGL).
 * Paste / adapt Waveshare ESP-IDF: examples/esp-idf/05_LVGL_WITH_RAM
 * Driver IC: CO5300; touch CST9217 (separate from face drawing for v1).
 *
 * v1 face: solid color / simple glyph per muse_status_t — no camera, no chat UI.
 */

esp_err_t display_face_init(void);
esp_err_t display_face_set_status(muse_status_t status);

#ifdef __cplusplus
}
#endif

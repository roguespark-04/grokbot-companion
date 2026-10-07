#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Playback path: ES8311 (I2C 0x18) over I2S TX + NS4150B PA (GPIO46).
 * Paste / adapt Waveshare: Arduino 08_ES8311 or ESP-IDF 06_I2SCodec /
 * BSP bsp_audio_codec_speaker_init(). Prefer BSP for PA_CTRL lifecycle.
 */

esp_err_t audio_playback_init(void);
esp_err_t audio_playback_stop(void);

/** Play PCM/WAV bytes (blocking or task — TBD). Stub no-ops. */
esp_err_t audio_playback_play(const uint8_t *data, size_t len);

/** Stream from HTTPS URL returned by Meridian (download then play). */
esp_err_t audio_playback_play_url(const char *url);

#ifdef __cplusplus
}
#endif

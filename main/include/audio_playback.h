#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Playback path: ES8311 (I2C 0x18) over I2S TX + NS4150B PA (GPIO46).
 * HARDWARE TODO: bsp_audio_codec_speaker_init() + esp_codec_dev_write().
 */

esp_err_t audio_playback_init(void);
esp_err_t audio_playback_stop(void);
esp_err_t audio_playback_set_volume(uint8_t pct);

/** Play a WAV blob (blocking). */
esp_err_t audio_playback_play(const uint8_t *wav, size_t len);

/** Download reply WAV from the relay and play it (blocking). */
esp_err_t audio_playback_play_url(const char *url);

/** Current output level 0..255 for the speaking avatar (RMS of the last chunk). */
uint8_t   audio_playback_level(void);

#ifdef __cplusplus
}
#endif

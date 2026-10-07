#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Capture path: ES7210 (I2C 0x40) over shared I2S TDM RX.
 * HARDWARE TODO: wire Waveshare BSP bsp_audio_codec_microphone_init() and downmix the
 * ES7210 TDM slots (MIC1+MIC2; MIC3 = ES8311 AEC reference) to mono 16 kHz s16.
 *
 * Three modes:
 *   - off          : I2S idle (lowest power)
 *   - armed        : mic running, frames go into a ~300 ms pre-roll ring only (VAD / wake)
 *   - recording    : frames appended to the turn buffer (PSRAM, up to MUSE_MAX_UTTERANCE_S)
 *
 * audio_capture_start(true) seeds the turn buffer with the pre-roll ring, so words spoken
 * just before a VAD trigger or during wake-from-sleep aren't lost.
 */

#define AUDIO_FRAME_SAMPLES  320   /* 20 ms @ 16 kHz */

esp_err_t audio_capture_init(void);
/** Power the mic path up/down without recording (pre-roll ring keeps filling). */
esp_err_t audio_capture_arm(bool on);
bool      audio_capture_is_armed(void);
/** Begin recording into the turn buffer; with_preroll copies the ring first. Arms if needed. */
esp_err_t audio_capture_start(bool with_preroll);
esp_err_t audio_capture_stop(void);
/** Drop whatever was recorded (e.g. short BOOT press that only woke the screen). */
void      audio_capture_discard(void);

/**
 * Pull one frame from I2S (blocks ≤ ~20 ms). Feeds pre-roll / turn buffer internally and
 * copies the samples into buf for VAD / level metering. Returns samples read (0 if none).
 */
size_t audio_capture_read(int16_t *buf, size_t max_samples);

/** Recorded duration so far (ms). */
uint32_t audio_capture_recorded_ms(void);

/** WAV (RIFF, PCM s16le, mono, CONFIG_MUSE_AUDIO_SAMPLE_RATE) of the turn buffer; caller frees. */
esp_err_t audio_capture_export_wav(uint8_t **out_wav, size_t *out_len);

#ifdef __cplusplus
}
#endif

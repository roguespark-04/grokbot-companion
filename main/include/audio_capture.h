#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Capture path: ES7210 (I2C 0x40) over shared I2S TDM RX.
 * Paste / adapt Waveshare ESP-IDF demo: examples/esp-idf/06_I2SCodec
 * or BSP bsp_audio_codec_microphone_init() / bsp_audio_init_voice_24k().
 *
 * ES7210 slots: MIC1 + MIC2 = onboard mics; MIC3 = ES8311 AEC reference; MIC4 N/C.
 * v1: downmix/select to mono 16 kHz PCM WAV for upload (see WEBHOOK_CONTRACT.md).
 */

esp_err_t audio_capture_init(void);
esp_err_t audio_capture_start(void);
esp_err_t audio_capture_stop(void);

/** Copy latest PCM frame into buf; returns bytes written or 0 if empty. */
size_t audio_capture_read(int16_t *buf, size_t max_samples);

/**
 * Build a WAV blob in RAM (or PSRAM) from the hold-to-talk buffer.
 * Caller frees with free(). Stub returns ESP_ERR_NOT_SUPPORTED until wired.
 */
esp_err_t audio_capture_export_wav(uint8_t **out_wav, size_t *out_len);

#ifdef __cplusplus
}
#endif

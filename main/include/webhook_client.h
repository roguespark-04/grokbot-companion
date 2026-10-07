#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device talks ONLY to the audio relay (see WEBHOOK_CONTRACT.md + relay/).
 *
 * Flow:
 *   1) POST /upload  WAV → { job_id, audio_url, result_url }
 *   2) Relay wakes Meridian (device never holds sender key)
 *   3) GET  /result/:job_id  poll until ready → { reply_audio_url } or WAV
 *
 * Kconfig MUSE_WEBHOOK_URL / MUSE_SENDER_BEARER_TOKEN are relay-side concepts;
 * on-device use MUSE_UPLOAD_URL (relay base) + device↔relay HMAC/bearer.
 */

typedef struct {
    char job_id[64];
    char audio_url[512];
    char result_url[512];
    char reply_audio_url[512];
    int  http_status;
} webhook_result_t;

esp_err_t webhook_client_init(void);

/** POST audio/wav to relay /upload; fills job_id, audio_url, result_url. */
esp_err_t webhook_client_upload_audio(const uint8_t *wav, size_t wav_len,
                                      webhook_result_t *out);

/**
 * After upload: poll relay result_url (preferred) until reply ready.
 * Does NOT POST to Meridian — relay owns that.
 */
esp_err_t webhook_client_wake(const webhook_result_t *upload,
                              webhook_result_t *out);

#ifdef __cplusplus
}
#endif

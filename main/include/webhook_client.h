#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bot_types.h"
#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device ↔ audio relay client (see WEBHOOK_CONTRACT.md + relay/README.md).
 *
 *   POST /upload              WAV + X-Target-Bot-Id / X-Voice-Id / X-Conversation-Id
 *                             → { job_id, audio_url, result_url, status_url }
 *   GET  /result/<job_id>     202 pending | 200 {status: ready, reply_audio_url} | error
 *   GET  /status/<bot_id>     {state, text, updated_at_ms, stale}
 *   GET  /bots, GET /voices   roster + placeholder voice list (JSON, cached in NVS)
 *
 * The relay wakes Meridian; this device never holds the Meridian sender key.
 * Auth: Authorization: Bearer CONFIG_MUSE_DEVICE_RELAY_TOKEN.
 */

typedef struct {
    char job_id[64];
    char audio_url[256];
    char result_url[256];
    char reply_audio_url[256];
    char error[96];
    int  http_status;
} webhook_result_t;

/** Routing for one voice turn — Meridian routes on target_bot_id; TTS side uses voice_id. */
typedef struct {
    const char *target_bot_id;
    const char *voice_id;          /* may be NULL/"" → relay uses bot default_voice_id */
    const char *conversation_id;   /* may be NULL/"" when no conversation is open */
} relay_route_t;

typedef struct {
    muse_status_t state;
    char          text[121];
    bool          stale;
    int64_t       updated_at_ms;
} relay_status_t;

typedef enum {
    RELAY_RESULT_PENDING = 0,
    RELAY_RESULT_READY,
    RELAY_RESULT_ERROR,
} relay_result_state_t;

esp_err_t webhook_client_init(void);

esp_err_t webhook_client_upload_audio(const uint8_t *wav, size_t wav_len,
                                      const relay_route_t *route, webhook_result_t *out);

/** One poll of result_url. Fills out->reply_audio_url when READY. */
esp_err_t webhook_client_poll_result(const webhook_result_t *upload, webhook_result_t *out,
                                     relay_result_state_t *state);

esp_err_t webhook_client_get_status(const char *bot_id, relay_status_t *out);

/** GET <relay>/<path> → malloc'd NUL-terminated body (caller frees). */
esp_err_t webhook_client_get_json(const char *path, char **out, size_t *out_len);

/** GET an absolute URL into a malloc'd buffer (reply WAV). max_len guards PSRAM use. */
esp_err_t webhook_client_download(const char *url, uint8_t **out, size_t *out_len, size_t max_len);

#ifdef __cplusplus
}
#endif

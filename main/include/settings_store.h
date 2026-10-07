/**
 * @file settings_store.h
 * @brief NVS-backed user settings (namespace "companion").
 *
 * Keys:
 *   last_bot   str   last selected / talked-to bot id (restored on boot + wake)
 *   talk_mode  u8    0 = press-to-talk (DEFAULT), 1 = always-listen
 *   volume     u8    0..100
 *   bright     u8    5..100 (%)
 *   sleep_s    u16   auto-sleep timeout seconds, 0 = never
 *   voice_<id> str   per-bot voice id (falls back to bot default_voice_id).
 *                    Keys longer than 15 chars use "v_<fnv1a32 hex>".
 *   bots_json  blob  last good GET /bots body (offline carousel)
 *   voices_js  blob  last good GET /voices body
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TALK_MODE_PTT = 0,     /* default: hold BOOT to talk */
    TALK_MODE_ALWAYS = 1,  /* energy VAD starts capture while a conversation is open */
} talk_mode_t;

typedef struct {
    char        last_bot_id[BOT_ID_MAX];
    talk_mode_t talk_mode;
    uint8_t     volume;
    uint8_t     brightness;
    uint16_t    sleep_s;
} device_settings_t;

#define SETTINGS_DEFAULT_VOLUME      70
#define SETTINGS_DEFAULT_BRIGHTNESS  80
#define SETTINGS_DEFAULT_SLEEP_S     30

/** Initializes NVS flash (erasing on version mismatch) and loads settings. */
esp_err_t settings_init(void);
void      settings_get(device_settings_t *out);

esp_err_t settings_set_last_bot(const char *bot_id);
esp_err_t settings_set_talk_mode(talk_mode_t mode);
esp_err_t settings_set_volume(uint8_t volume);
esp_err_t settings_set_brightness(uint8_t pct);
esp_err_t settings_set_sleep_s(uint16_t seconds);

/** Per-bot voice. Writes fallback (may be "") into out when unset. */
void      settings_get_voice(const char *bot_id, const char *fallback, char *out, size_t out_len);
esp_err_t settings_set_voice(const char *bot_id, const char *voice_id);

/** Raw cache blobs (JSON). get allocates *out (caller frees) and NUL-terminates. */
esp_err_t settings_blob_get(const char *key, char **out, size_t *out_len);
esp_err_t settings_blob_set(const char *key, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

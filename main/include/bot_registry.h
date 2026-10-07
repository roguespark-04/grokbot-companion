/**
 * @file bot_registry.h
 * @brief Bot roster + voice list: relay JSON → structs, NVS cache, built-in fallback.
 *
 * Owned by the app task (not thread-safe). The UI receives copies via ui_set_bots /
 * ui_set_voices, so the LVGL task never reads registry memory directly.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "bot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load NVS cache ("bots_json"/"voices_js"); fall back to the compiled-in roster. */
void bot_registry_init(void);

int               bot_registry_count(void);
const bot_info_t *bot_registry_get(int idx);
const bot_info_t *bot_registry_all(void);
int               bot_registry_index_of(const char *bot_id);   /* -1 if absent */
const char       *bot_registry_default_id(void);

int                 voice_registry_count(void);
const voice_info_t *voice_registry_get(int idx);
const voice_info_t *voice_registry_all(void);
int                 voice_registry_index_of(const char *voice_id);
bool                voice_registry_is_placeholder(void);

/**
 * Parse a GET /bots (or /voices) body. On success replaces the in-memory list and,
 * when persist=true and the body differs from the cache, writes it to NVS.
 * *changed is set when the parsed list differs from what was loaded before.
 */
esp_err_t bot_registry_update_from_json(const char *json, size_t len, bool persist, bool *changed);
esp_err_t voice_registry_update_from_json(const char *json, size_t len, bool persist, bool *changed);

#ifdef __cplusplus
}
#endif

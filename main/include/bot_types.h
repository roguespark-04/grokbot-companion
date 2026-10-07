/**
 * @file bot_types.h
 * @brief Bot roster + voice records shared by the registry, NVS cache and UI.
 *
 * Source of truth is the relay (GET /bots, GET /voices — see relay/bots.json and
 * relay/voices.json). The device caches the last good JSON in NVS so the carousel
 * works offline, and falls back to a compiled-in roster on first boot.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_ID_MAX      32
#define BOT_NAME_MAX    25
#define VOICE_ID_MAX    64
#define VOICE_NAME_MAX  41
#define VOICE_DESC_MAX  161
#define BOT_MAX         16
#define VOICE_MAX       32   /* the app lists 28 voices */

/**
 * Avatar silhouettes = the app's avatarShape values. Must stay in sync with relay
 * BOT_SHAPES. CIRCLE is the neutral default for bots the app hasn't given a shape.
 */
typedef enum {
    BOT_SHAPE_CIRCLE = 0,
    BOT_SHAPE_BLOB,       /* soft organic outline (slowly morphs while idle) */
    BOT_SHAPE_TEARDROP,   /* round bottom, soft point on top */
    BOT_SHAPE_CLOUD,      /* union of puffs over a flat-ish base */
    BOT_SHAPE_HEX,        /* softened hexagon */
    BOT_SHAPE_SQUIRCLE,   /* superellipse, n = 4 */
    BOT_SHAPE_COUNT,
} bot_shape_t;

typedef struct {
    char        id[BOT_ID_MAX];
    char        name[BOT_NAME_MAX];
    bot_shape_t shape;
    uint32_t    color;       /* 0xRRGGBB body (relay resolves palette names) */
    uint32_t    accent;      /* 0xRRGGBB highlight / rings / glow */
    uint32_t    rim;         /* 0xRRGGBB outline, valid when has_rim (dark bodies) */
    bool        has_rim;
    uint8_t     scale_pct;   /* body diameter as % of the screen (default 86) */
    int16_t     rotation;    /* deg */
    uint8_t     wobble;      /* 0..100 organic amount for blob/cloud */
    uint8_t     seed;        /* varies the blob outline per bot */
    bool        tbd;         /* app look unknown yet: neutral default */
    char        default_voice_id[VOICE_ID_MAX];   /* optional, "" = none */
    /* Future: sprite sheet id, see ui_avatar.h renderer ops. */
} bot_info_t;

typedef struct {
    char id[VOICE_ID_MAX];
    char name[VOICE_NAME_MAX];
    char description[VOICE_DESC_MAX];
    bool has_sample;   /* relay has GET /voices/<id>/sample.wav (optional, TODO via xAI TTS) */
} voice_info_t;

bot_shape_t bot_shape_from_str(const char *s);
const char *bot_shape_str(bot_shape_t s);
/** "#RRGGBB" → 0xRRGGBB; returns fallback on bad input. */
uint32_t bot_color_from_hex(const char *hex, uint32_t fallback);

#ifdef __cplusplus
}
#endif

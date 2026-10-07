/**
 * @file bot_types.h
 * @brief Bot roster + voice records shared by the registry, NVS cache and UI.
 *
 * Source of truth is the relay (GET /bots, GET /voices — see relay/bots.json and
 * relay/voices.json). The device caches the last good JSON in NVS so the carousel
 * works offline, and falls back to a compiled-in roster on first boot.
 */
#pragma once

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
#define VOICE_MAX       16

/** Procedural avatar silhouettes. Must stay in sync with relay BOT_SHAPES. */
typedef enum {
    BOT_SHAPE_CIRCLE = 0,
    BOT_SHAPE_SQUIRCLE,
    BOT_SHAPE_HEXAGON,
    BOT_SHAPE_DIAMOND,
    BOT_SHAPE_TRIANGLE,
    BOT_SHAPE_STAR,
    BOT_SHAPE_RING,
    BOT_SHAPE_PILL,
    BOT_SHAPE_OCTAGON,
    BOT_SHAPE_BLOB,      /* egg-ish (dr eggbot) */
    BOT_SHAPE_COUNT,
} bot_shape_t;

typedef struct {
    char        id[BOT_ID_MAX];
    char        name[BOT_NAME_MAX];
    bot_shape_t shape;
    uint32_t    color;       /* 0xRRGGBB body */
    uint32_t    accent;      /* 0xRRGGBB highlight / rings */
    char        default_voice_id[VOICE_ID_MAX];   /* optional, "" = none */
    /* Future: sprite_url / sprite sheet id — see ui_avatar.h renderer ops. */
} bot_info_t;

typedef struct {
    char id[VOICE_ID_MAX];
    char name[VOICE_NAME_MAX];
    char description[VOICE_DESC_MAX];
} voice_info_t;

bot_shape_t bot_shape_from_str(const char *s);
const char *bot_shape_str(bot_shape_t s);
/** "#RRGGBB" → 0xRRGGBB; returns fallback on bad input. */
uint32_t bot_color_from_hex(const char *hex, uint32_t fallback);

#ifdef __cplusplus
}
#endif

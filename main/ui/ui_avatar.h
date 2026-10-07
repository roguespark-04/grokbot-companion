/**
 * @file ui_avatar.h
 * @brief Data-driven bot avatar (procedural today, sprite/bitmap later).
 *
 * We can't pull the Grok Bot app's real avatar art or animations, so each bot is drawn
 * from its /bots record: shape + color + accent (relay/bots.json). Animations:
 *   idle      slow breathing pulse + blink
 *   listening ripple rings
 *   thinking  orbiting dots
 *   working   faster orbit in the accent color
 *   speaking  amplitude-driven scale + mouth
 *   error     shake + red tint
 *
 * The renderer is a vtable (ui_avatar_renderer_t). To swap in real art, add e.g. a
 * "sprite" renderer that draws frames from an lv_image sprite sheet per anim state and
 * select it per bot (future bot_info_t.sprite field) — callers don't change.
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"
#include "bot_types.h"
#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AVATAR_ANIM_STATIC = 0,   /* carousel neighbors: no animation (CPU / power) */
    AVATAR_ANIM_IDLE,
    AVATAR_ANIM_LISTENING,
    AVATAR_ANIM_THINKING,
    AVATAR_ANIM_WORKING,
    AVATAR_ANIM_SPEAKING,
    AVATAR_ANIM_ERROR,
} avatar_anim_t;

typedef struct ui_avatar ui_avatar_t;

typedef struct {
    const char *name;
    void (*build)(ui_avatar_t *av);                       /* create LVGL objects */
    void (*set_bot)(ui_avatar_t *av, const bot_info_t *bot);
    void (*set_anim)(ui_avatar_t *av, avatar_anim_t anim);
    void (*set_level)(ui_avatar_t *av, uint8_t level);
    void (*set_base_scale)(ui_avatar_t *av, int32_t scale_256);
} ui_avatar_renderer_t;

extern const ui_avatar_renderer_t ui_avatar_renderer_procedural;

ui_avatar_t *ui_avatar_create(lv_obj_t *parent, const ui_avatar_renderer_t *renderer);
lv_obj_t    *ui_avatar_obj(ui_avatar_t *av);
void         ui_avatar_set_bot(ui_avatar_t *av, const bot_info_t *bot);
void         ui_avatar_set_anim(ui_avatar_t *av, avatar_anim_t anim);
void         ui_avatar_set_level(ui_avatar_t *av, uint8_t level);
void         ui_avatar_set_base_scale(ui_avatar_t *av, int32_t scale_256);

avatar_anim_t ui_avatar_anim_for_status(muse_status_t s);

#ifdef __cplusplus
}
#endif

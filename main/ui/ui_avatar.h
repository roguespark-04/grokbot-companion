/**
 * @file ui_avatar.h
 * @brief Full-screen, data-driven bot avatar (procedural today, sprite later).
 *
 * Each bot is drawn from its /bots record, which mirrors the app profile's avatarShape /
 * avatarColor (relay/bots.json): shape (blob | teardrop | cloud | hex | squircle |
 * circle) + color + accent (+ optional rim for dark bodies, scale, rotation, wobble, seed).
 * The body fills ~86 % of the 466 px round screen. Animations:
 *   idle      breathing + blink (blob/cloud outlines slowly morph)
 *   listening ripple rings + quicker breath
 *   thinking  dots orbiting the screen rim
 *   working   faster accent orbit + breath
 *   speaking  amplitude-driven scale + waveform mouth
 *   error     shake + red tint
 *   glow      (orthogonal) soft accent rim pulse while a conversation is open
 *
 * Everything is painted in one draw callback, so neighbours can be faded with the
 * object's opa style and scaled with set_base_scale without layer buffers. The renderer
 * is a vtable (ui_avatar_renderer_t): a sprite-sheet renderer can replace it per bot
 * later without touching callers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "bot_types.h"
#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AVATAR_ANIM_STATIC = 0,   /* carousel neighbours mid-drag: no animation (CPU / power) */
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
    void (*set_glow)(ui_avatar_t *av, bool on);
} ui_avatar_renderer_t;

extern const ui_avatar_renderer_t ui_avatar_renderer_procedural;

/** Root object is UI_W x UI_H; move it with lv_obj_set_x(), fade it with opa style. */
ui_avatar_t *ui_avatar_create(lv_obj_t *parent, const ui_avatar_renderer_t *renderer);
lv_obj_t    *ui_avatar_obj(ui_avatar_t *av);
void         ui_avatar_set_bot(ui_avatar_t *av, const bot_info_t *bot);
void         ui_avatar_set_anim(ui_avatar_t *av, avatar_anim_t anim);
void         ui_avatar_set_level(ui_avatar_t *av, uint8_t level);
void         ui_avatar_set_base_scale(ui_avatar_t *av, int32_t scale_256);
void         ui_avatar_set_glow(ui_avatar_t *av, bool on);

avatar_anim_t ui_avatar_anim_for_status(muse_status_t s);

#ifdef __cplusplus
}
#endif

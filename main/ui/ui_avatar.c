/**
 * Procedural avatar renderer (LVGL 9).
 */
#include "ui_avatar.h"

#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"

#define RING_COUNT  3
#define DOT_COUNT   3

struct ui_avatar {
    const ui_avatar_renderer_t *r;
    lv_obj_t   *root;
    lv_obj_t   *body;
    lv_obj_t   *eye[2];
    lv_obj_t   *mouth;
    lv_obj_t   *ring[RING_COUNT];
    lv_obj_t   *dot[DOT_COUNT];
    lv_timer_t *level_timer;
    bot_info_t  bot;
    avatar_anim_t anim;
    int32_t     base_scale;   /* 256 = 1.0 (carousel peek) */
    int32_t     breath;       /* 0..1000 */
    int32_t     level_target;
    int32_t     level_cur;
    bool        error_tint;
};

/* Per-shape face placement (data-driven; tweak without touching draw code). */
typedef struct {
    int8_t eye_dy;     /* px from body center */
    int8_t eye_gap;    /* half distance between eyes */
    uint8_t eye_w, eye_h;
    bool shine;        /* accent highlight (off where it would poke outside the silhouette) */
} shape_face_t;

static const shape_face_t SHAPE_FACE[BOT_SHAPE_COUNT] = {
    [BOT_SHAPE_CIRCLE]   = {-8, 24, 16, 22, true},
    [BOT_SHAPE_SQUIRCLE] = {-8, 26, 16, 22, true},
    [BOT_SHAPE_HEXAGON]  = {-6, 24, 16, 22, true},
    [BOT_SHAPE_DIAMOND]  = {-4, 20, 14, 20, false},
    [BOT_SHAPE_TRIANGLE] = {14, 18, 14, 18, false},
    [BOT_SHAPE_STAR]     = {-2, 16, 12, 18, false},
    [BOT_SHAPE_RING]     = {-6, 18, 12, 18, false},
    [BOT_SHAPE_PILL]     = {-6, 30, 16, 20, true},
    [BOT_SHAPE_OCTAGON]  = {-8, 24, 16, 22, true},
    [BOT_SHAPE_BLOB]     = {-2, 20, 14, 20, true},
};

/* ------------------------------------------------------------------ helpers */

static lv_color_t c24(uint32_t rgb) { return lv_color_hex(rgb); }

static bool is_light(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 > 150;
}

static void apply_scale(ui_avatar_t *av)
{
    /* breath: up to +5 %, speaking level: up to +19 % */
    int32_t s = 256 + (av->breath * 13) / 1000 + (av->level_cur * 48) / 255;
    s = (s * av->base_scale) / 256;
    lv_obj_set_style_transform_scale(av->body, s, 0);
}

/* ------------------------------------------------------------------ drawing */

static void fill_rect(lv_layer_t *layer, const lv_area_t *a, lv_color_t col, lv_opa_t opa,
                      int32_t radius, int32_t border)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    if (border > 0) {
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = col;
        d.border_opa = opa;
        d.border_width = border;
    } else {
        d.bg_color = col;
        d.bg_opa = opa;
    }
    lv_draw_rect(layer, &d, a);
}

static void fill_polygon(lv_layer_t *layer, int32_t cx, int32_t cy, const lv_point_t *v, int n,
                         lv_color_t col)
{
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = col;
    t.opa = LV_OPA_COVER;
    /* Fan from the center (all our polygons are star-shaped around it). Drawn twice so
     * anti-aliased seams between neighbouring triangles disappear. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            const lv_point_t *a = &v[i];
            const lv_point_t *b = &v[(i + 1) % n];
            t.p[0].x = cx;   t.p[0].y = cy;
            t.p[1].x = a->x; t.p[1].y = a->y;
            t.p[2].x = b->x; t.p[2].y = b->y;
            lv_draw_triangle(layer, &t);
        }
    }
}

static int regular_poly(lv_point_t *out, int n, int32_t cx, int32_t cy, int32_t r,
                        int32_t start_deg, int32_t inner_r)
{
    int cnt = 0;
    int steps = inner_r ? n * 2 : n;
    for (int i = 0; i < steps; i++) {
        int32_t ang = start_deg + (360 * i) / steps;
        int32_t rr = (inner_r && (i & 1)) ? inner_r : r;
        out[cnt].x = cx + (lv_trigo_cos((int16_t)ang) * rr) / LV_TRIGO_SIN_MAX;
        out[cnt].y = cy + (lv_trigo_sin((int16_t)ang) * rr) / LV_TRIGO_SIN_MAX;
        cnt++;
    }
    return cnt;
}

static void draw_body_cb(lv_event_t *e)
{
    ui_avatar_t *av = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    int32_t cx = a.x1 + w / 2, cy = a.y1 + h / 2, r = w / 2;
    uint32_t base = av->error_tint ? 0xDC2626 : av->bot.color;
    lv_color_t col = c24(base);
    lv_point_t v[16];
    int n = 0;

    switch (av->bot.shape) {
    case BOT_SHAPE_SQUIRCLE:
        fill_rect(layer, &a, col, LV_OPA_COVER, w * 28 / 100, 0);
        break;
    case BOT_SHAPE_PILL: {
        lv_area_t p = a;
        p.y1 = cy - h * 33 / 100;
        p.y2 = cy + h * 33 / 100;
        fill_rect(layer, &p, col, LV_OPA_COVER, LV_RADIUS_CIRCLE, 0);
        break;
    }
    case BOT_SHAPE_BLOB: {   /* egg: tall capsule, slightly narrower */
        lv_area_t p = a;
        p.x1 = cx - w * 40 / 100;
        p.x2 = cx + w * 40 / 100;
        fill_rect(layer, &p, col, LV_OPA_COVER, LV_RADIUS_CIRCLE, 0);
        break;
    }
    case BOT_SHAPE_RING:
        fill_rect(layer, &a, col, LV_OPA_COVER, LV_RADIUS_CIRCLE, w * 17 / 100);
        break;
    case BOT_SHAPE_HEXAGON:
        n = regular_poly(v, 6, cx, cy, r, -90, 0);
        break;
    case BOT_SHAPE_OCTAGON:
        n = regular_poly(v, 8, cx, cy, r, -90 + 22, 0);
        break;
    case BOT_SHAPE_DIAMOND:
        n = regular_poly(v, 4, cx, cy, r, -90, 0);
        break;
    case BOT_SHAPE_TRIANGLE:
        n = regular_poly(v, 3, cx, cy + r / 6, r + r / 8, -90, 0);
        cy += r / 6;
        break;
    case BOT_SHAPE_STAR:
        n = regular_poly(v, 5, cx, cy, r, -90, r * 50 / 100);
        break;
    case BOT_SHAPE_CIRCLE:
    default:
        fill_rect(layer, &a, col, LV_OPA_COVER, LV_RADIUS_CIRCLE, 0);
        break;
    }
    if (n) fill_polygon(layer, cx, cy, v, n, col);

    /* soft shine (accent) where it stays inside the silhouette */
    if (SHAPE_FACE[av->bot.shape].shine) {
        lv_area_t s = {
            .x1 = cx - w * 30 / 100, .y1 = cy - h * 34 / 100,
            .x2 = cx - w * 8 / 100,  .y2 = cy - h * 20 / 100,
        };
        fill_rect(layer, &s, c24(av->bot.accent), LV_OPA_40, LV_RADIUS_CIRCLE, 0);
    }
}

/* ------------------------------------------------------------------ animations */

static void breath_exec(void *var, int32_t v)
{
    ui_avatar_t *av = var;
    av->breath = v;
    apply_scale(av);
}

static void ring_exec(void *var, int32_t v)
{
    lv_obj_t *ring = var;
    int32_t s = UI_AVATAR_BODY + ((UI_AVATAR_BOX - UI_AVATAR_BODY) * v) / 1000;
    lv_obj_set_size(ring, s, s);
    lv_obj_set_style_border_opa(ring, (lv_opa_t)(220 * (1000 - v) / 1000), 0);
}

static void orbit_exec(void *var, int32_t v)
{
    ui_avatar_t *av = var;
    int32_t rad = UI_AVATAR_BODY / 2 + 26;
    for (int i = 0; i < DOT_COUNT; i++) {
        int16_t ang = (int16_t)((v / 10 + i * 120) % 360);
        int32_t x = (lv_trigo_cos(ang) * rad) / LV_TRIGO_SIN_MAX;
        int32_t y = (lv_trigo_sin(ang) * rad) / LV_TRIGO_SIN_MAX;
        lv_obj_align(av->dot[i], LV_ALIGN_CENTER, x, y);
    }
}

static void eye_exec(void *var, int32_t v)
{
    lv_obj_set_height((lv_obj_t *)var, v);
}

static void shake_exec(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void level_timer_cb(lv_timer_t *t)
{
    ui_avatar_t *av = lv_timer_get_user_data(t);
    int32_t d = av->level_target - av->level_cur;
    av->level_cur += d / 3 + (d > 0 ? 1 : (d < 0 ? -1 : 0));
    if (av->level_cur < 0) av->level_cur = 0;
    if (av->mouth) {
        lv_obj_set_height(av->mouth, 4 + (av->level_cur * 20) / 255);
    }
    apply_scale(av);
}

static void stop_all(ui_avatar_t *av)
{
    lv_anim_delete(av, NULL);
    for (int i = 0; i < RING_COUNT; i++) {
        lv_anim_delete(av->ring[i], NULL);
        lv_obj_add_flag(av->ring[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < DOT_COUNT; i++) lv_obj_add_flag(av->dot[i], LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) {
        lv_anim_delete(av->eye[i], NULL);
        lv_obj_set_height(av->eye[i], SHAPE_FACE[av->bot.shape].eye_h);
    }
    lv_anim_delete(av->root, NULL);
    lv_obj_set_style_translate_x(av->root, 0, 0);
    lv_obj_add_flag(av->mouth, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(av->level_timer);
    av->breath = 0;
    av->level_cur = av->level_target = 0;
    if (av->error_tint) {
        av->error_tint = false;
        lv_obj_invalidate(av->body);
    }
    apply_scale(av);
}

static void start_breath(ui_avatar_t *av, uint32_t period_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, av);
    lv_anim_set_exec_cb(&a, breath_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_reverse_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void start_blink(ui_avatar_t *av)
{
    int32_t h = SHAPE_FACE[av->bot.shape].eye_h;
    for (int i = 0; i < 2; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, av->eye[i]);
        lv_anim_set_exec_cb(&a, eye_exec);
        lv_anim_set_values(&a, h, 3);
        lv_anim_set_duration(&a, 90);
        lv_anim_set_reverse_duration(&a, 120);
        lv_anim_set_repeat_delay(&a, 3800);
        lv_anim_set_delay(&a, 1500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

static void start_ripples(ui_avatar_t *av)
{
    for (int i = 0; i < RING_COUNT; i++) {
        lv_obj_remove_flag(av->ring[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(av->ring[i], c24(av->bot.accent), 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, av->ring[i]);
        lv_anim_set_exec_cb(&a, ring_exec);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, 1500);
        lv_anim_set_delay(&a, i * 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

static void start_orbit(ui_avatar_t *av, uint32_t period_ms, lv_color_t col)
{
    for (int i = 0; i < DOT_COUNT; i++) {
        lv_obj_remove_flag(av->dot[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(av->dot[i], col, 0);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, av);
    lv_anim_set_exec_cb(&a, orbit_exec);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void start_shake(ui_avatar_t *av)
{
    av->error_tint = true;
    lv_obj_invalidate(av->body);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, av->root);
    lv_anim_set_exec_cb(&a, shake_exec);
    lv_anim_set_values(&a, -10, 10);
    lv_anim_set_duration(&a, 70);
    lv_anim_set_reverse_duration(&a, 70);
    lv_anim_set_repeat_count(&a, 4);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ renderer ops */

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void proc_build(ui_avatar_t *av)
{
    lv_obj_set_size(av->root, UI_AVATAR_BOX, UI_AVATAR_BOX);

    for (int i = 0; i < RING_COUNT; i++) {
        lv_obj_t *r = plain(av->root);
        lv_obj_set_size(r, UI_AVATAR_BODY, UI_AVATAR_BODY);
        lv_obj_align(r, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(r, 3, 0);
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
        av->ring[i] = r;
    }

    av->body = plain(av->root);
    lv_obj_set_size(av->body, UI_AVATAR_BODY, UI_AVATAR_BODY);
    lv_obj_align(av->body, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_transform_pivot_x(av->body, UI_AVATAR_BODY / 2, 0);
    lv_obj_set_style_transform_pivot_y(av->body, UI_AVATAR_BODY / 2, 0);
    lv_obj_add_event_cb(av->body, draw_body_cb, LV_EVENT_DRAW_MAIN, av);

    for (int i = 0; i < 2; i++) {
        av->eye[i] = plain(av->body);
        lv_obj_set_style_bg_opa(av->eye[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(av->eye[i], LV_RADIUS_CIRCLE, 0);
    }
    av->mouth = plain(av->body);
    lv_obj_set_style_bg_opa(av->mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(av->mouth, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(av->mouth, 30, 4);
    lv_obj_add_flag(av->mouth, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < DOT_COUNT; i++) {
        lv_obj_t *d = plain(av->root);
        lv_obj_set_size(d, 14 - i * 3, 14 - i * 3);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        av->dot[i] = d;
    }
    av->level_timer = lv_timer_create(level_timer_cb, 33, av);
    lv_timer_pause(av->level_timer);
}

static void proc_set_bot(ui_avatar_t *av, const bot_info_t *bot)
{
    av->bot = *bot;
    if (av->bot.shape >= BOT_SHAPE_COUNT) av->bot.shape = BOT_SHAPE_CIRCLE;
    const shape_face_t *f = &SHAPE_FACE[av->bot.shape];
    uint32_t eye_rgb = (av->bot.shape == BOT_SHAPE_RING) ? av->bot.color
                       : (is_light(av->bot.color) ? 0x111318 : 0xFFFFFF);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(av->eye[i], f->eye_w, f->eye_h);
        lv_obj_set_style_bg_color(av->eye[i], c24(eye_rgb), 0);
        lv_obj_align(av->eye[i], LV_ALIGN_CENTER, i ? f->eye_gap : -f->eye_gap, f->eye_dy);
    }
    lv_obj_set_style_bg_color(av->mouth, c24(eye_rgb), 0);
    lv_obj_align(av->mouth, LV_ALIGN_CENTER, 0, f->eye_dy + 30);
    lv_obj_invalidate(av->body);
    avatar_anim_t cur = av->anim;
    av->anim = (avatar_anim_t)-1;
    av->r->set_anim(av, cur);   /* restart with new colors */
}

static void proc_set_anim(ui_avatar_t *av, avatar_anim_t anim)
{
    if (anim == av->anim) return;
    stop_all(av);
    av->anim = anim;
    switch (anim) {
    case AVATAR_ANIM_IDLE:
        start_breath(av, 2400);
        start_blink(av);
        break;
    case AVATAR_ANIM_LISTENING:
        start_breath(av, 1200);
        start_ripples(av);
        break;
    case AVATAR_ANIM_THINKING:
        start_orbit(av, 1600, UI_COL_TEXT);
        start_blink(av);
        break;
    case AVATAR_ANIM_WORKING:
        start_orbit(av, 800, c24(av->bot.accent));
        start_breath(av, 900);
        break;
    case AVATAR_ANIM_SPEAKING:
        lv_obj_remove_flag(av->mouth, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(av->level_timer);
        break;
    case AVATAR_ANIM_ERROR:
        start_shake(av);
        break;
    case AVATAR_ANIM_STATIC:
    default:
        break;
    }
}

static void proc_set_level(ui_avatar_t *av, uint8_t level)
{
    av->level_target = level;
}

static void proc_set_base_scale(ui_avatar_t *av, int32_t scale_256)
{
    if (scale_256 == av->base_scale) return;
    av->base_scale = scale_256;
    apply_scale(av);
}

const ui_avatar_renderer_t ui_avatar_renderer_procedural = {
    .name = "procedural",
    .build = proc_build,
    .set_bot = proc_set_bot,
    .set_anim = proc_set_anim,
    .set_level = proc_set_level,
    .set_base_scale = proc_set_base_scale,
};

/* ------------------------------------------------------------------ public */

ui_avatar_t *ui_avatar_create(lv_obj_t *parent, const ui_avatar_renderer_t *renderer)
{
    ui_avatar_t *av = lv_malloc_zeroed(sizeof(*av));
    LV_ASSERT_MALLOC(av);
    av->r = renderer ? renderer : &ui_avatar_renderer_procedural;
    av->base_scale = 256;
    av->anim = AVATAR_ANIM_STATIC;
    av->root = plain(parent);
    av->r->build(av);
    return av;
}

lv_obj_t *ui_avatar_obj(ui_avatar_t *av) { return av->root; }
void ui_avatar_set_bot(ui_avatar_t *av, const bot_info_t *bot) { av->r->set_bot(av, bot); }
void ui_avatar_set_anim(ui_avatar_t *av, avatar_anim_t anim) { av->r->set_anim(av, anim); }
void ui_avatar_set_level(ui_avatar_t *av, uint8_t level) { av->r->set_level(av, level); }
void ui_avatar_set_base_scale(ui_avatar_t *av, int32_t s) { av->r->set_base_scale(av, s); }

avatar_anim_t ui_avatar_anim_for_status(muse_status_t s)
{
    switch (s) {
    case MUSE_STATUS_LISTENING: return AVATAR_ANIM_LISTENING;
    case MUSE_STATUS_UPLOADING:
    case MUSE_STATUS_THINKING:  return AVATAR_ANIM_THINKING;
    case MUSE_STATUS_WORKING:   return AVATAR_ANIM_WORKING;
    case MUSE_STATUS_SPEAKING:  return AVATAR_ANIM_SPEAKING;
    case MUSE_STATUS_ERROR:     return AVATAR_ANIM_ERROR;
    case MUSE_STATUS_IDLE:
    default:                    return AVATAR_ANIM_IDLE;
    }
}

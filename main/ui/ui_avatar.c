/**
 * Procedural full-screen avatar renderer (LVGL 9). See ui_avatar.h.
 *
 * All geometry is in "unit" space (body radius = 1, y down) and scaled at draw time, so
 * one code path serves the 400 px home avatar and the shrunken neighbours mid-swipe.
 * Shapes are star-shaped polygons around a fan centre, filled as triangle fans.
 *
 * PERF TODO (hardware): the body is repainted on every animation tick. If the CO5300
 * QSPI flush can't keep ~30 fps for a 400 px region, cache the silhouette per (bot,
 * scale step) in a PSRAM A8 mask and only repaint eyes/mouth/rings.
 */
#include "ui_avatar.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"

#define OUTLINE_N   48
#define PI_F        3.14159265f

struct ui_avatar {
    const ui_avatar_renderer_t *r;
    lv_obj_t   *root;
    lv_timer_t *level_timer;
    bot_info_t  bot;
    avatar_anim_t anim;
    int32_t     base_scale;   /* 256 = 1.0 */
    int32_t     breath;       /* 0..1000 */
    int32_t     blink;        /* 0 open .. 1000 closed */
    int32_t     ripple;       /* 0..1000 */
    int32_t     orbit;        /* 0..3600 (0.1 deg) */
    int32_t     morph;        /* 0..3600 (0.1 deg) blob/cloud outline drift */
    int32_t     glow_ph;      /* 0..1000 */
    int32_t     level_target;
    int32_t     level_cur;
    float       speak_ph;
    bool        glow;
    bool        error_tint;
    bool        orbit_on, ripple_on, mouth_on;
    uint32_t    orbit_rgb;
};

/* Face placement per shape, in unit space (data-driven: tune without touching draw code). */
typedef struct {
    float eye_y, eye_gap, eye_w, eye_h, mouth_y;
    bool  shine;
    float shine_x, shine_y;
} shape_face_t;

static const shape_face_t SHAPE_FACE[BOT_SHAPE_COUNT] = {
    [BOT_SHAPE_CIRCLE]   = {-0.08f, 0.30f, 0.15f, 0.23f, 0.34f, true, -0.40f, -0.46f},
    [BOT_SHAPE_BLOB]     = {-0.06f, 0.29f, 0.15f, 0.23f, 0.34f, true, -0.38f, -0.44f},
    [BOT_SHAPE_TEARDROP] = { 0.10f, 0.25f, 0.14f, 0.21f, 0.33f, true, -0.36f,  0.00f},
    [BOT_SHAPE_CLOUD]    = { 0.04f, 0.28f, 0.14f, 0.21f, 0.34f, true, -0.30f, -0.44f},
    [BOT_SHAPE_HEX]      = {-0.06f, 0.28f, 0.15f, 0.23f, 0.34f, true, -0.36f, -0.42f},
    [BOT_SHAPE_SQUIRCLE] = {-0.08f, 0.30f, 0.15f, 0.23f, 0.34f, true, -0.42f, -0.46f},
};

/* Cloud = union of puffs (cx, cy, r) in unit space; base row + three top puffs. */
static const float CLOUD_PUFFS[][3] = {
    {-0.46f, 0.28f, 0.42f}, {0.46f, 0.28f, 0.42f}, {0.00f, 0.30f, 0.50f},
    {-0.42f, -0.06f, 0.40f}, {0.02f, -0.32f, 0.52f}, {0.46f, -0.08f, 0.38f},
};
#define CLOUD_PUFF_N (sizeof(CLOUD_PUFFS) / sizeof(CLOUD_PUFFS[0]))

/* ------------------------------------------------------------------ helpers */

static lv_color_t c24(uint32_t rgb) { return lv_color_hex(rgb); }

static bool is_light(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 > 150;
}

static void redraw(ui_avatar_t *av) { lv_obj_invalidate(av->root); }

static float wob(const ui_avatar_t *av) { return av->bot.wobble / 50.0f; }

/** Unit-space outline. Returns fan centre in *fx,*fy (unit space). */
static void shape_outline(const ui_avatar_t *av, float ux[OUTLINE_N], float uy[OUTLINE_N],
                          float *fx, float *fy)
{
    const float m = av->morph * (2 * PI_F / 3600.0f);
    const float seed = av->bot.seed * 0.0245f;   /* 0..2pi */
    *fx = 0;
    *fy = 0;
    for (int i = 0; i < OUTLINE_N; i++) {
        float t = 2 * PI_F * i / OUTLINE_N;
        float c = cosf(t), s = sinf(t), x, y;
        switch (av->bot.shape) {
        case BOT_SHAPE_BLOB: {
            float w = wob(av);
            float rr = 1 + w * (0.05f * sinf(2 * t + seed + m) + 0.04f * sinf(3 * t + 2 * seed - m) +
                                0.025f * sinf(5 * t + 3 * seed + 2 * m));
            rr /= 1 + w * 0.115f;
            x = c * rr;
            y = s * rr;
            break;
        }
        case BOT_SHAPE_TEARDROP: {
            /* x = sin t * sin(t/2), y = -cos t: soft point at the top, round bottom */
            x = 0.95f * s * sinf(t / 2);
            y = -0.95f * c;
            *fy = 0.30f;
            break;
        }
        case BOT_SHAPE_CLOUD: {
            /* ray-cast the union of puffs from the origin (inside the centre puff) */
            float best = 0.3f, w = wob(av);
            for (size_t k = 0; k < CLOUD_PUFF_N; k++) {
                float px = CLOUD_PUFFS[k][0], py = CLOUD_PUFFS[k][1];
                float pr = CLOUD_PUFFS[k][2] * (1 + 0.02f * w * sinf(m + k));
                float b = px * c + py * s;
                float disc = b * b - (px * px + py * py - pr * pr);
                if (disc >= 0) {
                    float d = b + sqrtf(disc);
                    if (d > best) best = d;
                }
            }
            x = c * best;
            y = s * best;
            break;
        }
        case BOT_SHAPE_HEX: {
            /* pointy-top hexagon, corners softened by clipping to a circle */
            float a = fmodf(t + PI_F / 2 + 2 * PI_F, PI_F / 3);
            float rr = cosf(PI_F / 6) / cosf(a - PI_F / 6);
            if (rr > 0.95f) rr = 0.95f;
            rr /= 0.95f;
            x = c * rr;
            y = s * rr;
            break;
        }
        case BOT_SHAPE_SQUIRCLE: {
            /* superellipse n = 4 */
            x = 0.92f * copysignf(sqrtf(fabsf(c)), c);
            y = 0.92f * copysignf(sqrtf(fabsf(s)), s);
            break;
        }
        case BOT_SHAPE_CIRCLE:
        default:
            x = c;
            y = s;
            break;
        }
        if (av->bot.rotation) {
            float ra = av->bot.rotation * PI_F / 180.0f, cr = cosf(ra), sr = sinf(ra);
            float nx = x * cr - y * sr, ny = x * sr + y * cr;
            x = nx;
            y = ny;
        }
        ux[i] = x;
        uy[i] = y;
    }
}

static void fill_ellipse(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t w, int32_t h,
                         lv_color_t col, lv_opa_t opa)
{
    if (w < 1 || h < 1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = col;
    d.bg_opa = opa;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {cx - w / 2, cy - h / 2, cx - w / 2 + w - 1, cy - h / 2 + h - 1};
    lv_draw_rect(layer, &d, &a);
}

static void ring(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, int32_t width,
                 lv_color_t col, lv_opa_t opa)
{
    if (opa <= LV_OPA_MIN || r <= width) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r;
    d.width = width;
    d.start_angle = 0;
    d.end_angle = 360;
    d.color = col;
    d.opa = opa;
    lv_draw_arc(layer, &d);
}

/* ------------------------------------------------------------------ drawing */

static void draw_cb(lv_event_t *e)
{
    ui_avatar_t *av = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(av->root, &a);
    const int32_t cx = a.x1 + lv_area_get_width(&a) / 2;
    const int32_t cy = a.y1 + lv_area_get_height(&a) / 2;
    const shape_face_t *f = &SHAPE_FACE[av->bot.shape < BOT_SHAPE_COUNT ? av->bot.shape : 0];

    float R = (UI_W / 2.0f) * (av->bot.scale_pct ? av->bot.scale_pct : 86) / 100.0f;
    R *= 1 + av->breath * 0.025f / 1000 + av->level_cur * 0.06f / 255;
    R *= av->base_scale / 256.0f;

    uint32_t body_rgb = av->bot.color;
    lv_color_t body = av->error_tint ? lv_color_mix(c24(0xDC2626), c24(body_rgb), 170) : c24(body_rgb);
    lv_color_t accent = c24(av->bot.accent);
    lv_color_t eye = c24(is_light(body_rgb) && !av->error_tint ? 0x111318 : 0xFFFFFF);

    /* conversation-open cue: soft accent pulse along the round screen edge (centred on
     * the parent = the screen, even though the avatar itself sits slightly high) */
    if (av->glow) {
        lv_area_t pa;
        lv_obj_get_coords(lv_obj_get_parent(av->root), &pa);
        int32_t gx = pa.x1 + lv_area_get_width(&pa) / 2, gy = pa.y1 + lv_area_get_height(&pa) / 2;
        lv_opa_t go = (lv_opa_t)(45 + (70 * av->glow_ph) / 1000);
        ring(layer, gx, gy, UI_W / 2 - 1, 7, accent, go);
        ring(layer, gx, gy, UI_W / 2 - 8, 6, accent, go / 3);
    }

    /* listening ripples between the body and the screen edge */
    if (av->ripple_on) {
        for (int k = 0; k < 3; k++) {
            int32_t ph = (av->ripple + k * 333) % 1000;
            int32_t rr = (int32_t)(R * (1.0f + 0.16f * ph / 1000));
            if (rr > UI_W / 2 - 3) rr = UI_W / 2 - 3;
            ring(layer, cx, cy, rr, 4, accent, (lv_opa_t)(200 * (1000 - ph) / 1000));
        }
    }

    /* body */
    float ux[OUTLINE_N], uy[OUTLINE_N], fx, fy;
    shape_outline(av, ux, uy, &fx, &fy);
    lv_point_t pts[OUTLINE_N];
    for (int i = 0; i < OUTLINE_N; i++) {
        pts[i].x = cx + (int32_t)lroundf(ux[i] * R);
        pts[i].y = cy + (int32_t)lroundf(uy[i] * R);
    }
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = body;
    t.opa = LV_OPA_COVER;
    const int32_t fcx = cx + (int32_t)(fx * R), fcy = cy + (int32_t)(fy * R);
    for (int pass = 0; pass < 2; pass++) {   /* 2nd pass hides anti-aliased fan seams */
        for (int i = 0; i < OUTLINE_N; i++) {
            const lv_point_t *p = &pts[i], *q = &pts[(i + 1) % OUTLINE_N];
            t.p[0].x = fcx;  t.p[0].y = fcy;
            t.p[1].x = p->x; t.p[1].y = p->y;
            t.p[2].x = q->x; t.p[2].y = q->y;
            lv_draw_triangle(layer, &t);
        }
    }
    /* light rim so dark bodies (e.g. black) stay visible on the black AMOLED */
    if (av->bot.has_rim) {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = c24(av->bot.rim);
        l.width = LV_MAX(2, (int32_t)(R * 0.015f));
        l.opa = LV_OPA_COVER;
        l.round_start = l.round_end = 1;
        for (int i = 0; i < OUTLINE_N; i++) {
            const lv_point_t *p = &pts[i], *q = &pts[(i + 1) % OUTLINE_N];
            l.p1.x = p->x; l.p1.y = p->y;
            l.p2.x = q->x; l.p2.y = q->y;
            lv_draw_line(layer, &l);
        }
    }
    if (f->shine) {
        fill_ellipse(layer, cx + (int32_t)(f->shine_x * R), cy + (int32_t)(f->shine_y * R),
                     (int32_t)(0.26f * R), (int32_t)(0.14f * R), accent, LV_OPA_30);
    }

    /* eyes */
    int32_t ew = (int32_t)(f->eye_w * R), eh_full = (int32_t)(f->eye_h * R);
    int32_t eh = eh_full - (eh_full * 85 * av->blink) / (100 * 1000);
    if (eh < 3) eh = 3;
    int32_t ey = cy + (int32_t)(f->eye_y * R), gap = (int32_t)(f->eye_gap * R);
    fill_ellipse(layer, cx - gap, ey, ew, eh, eye, LV_OPA_COVER);
    fill_ellipse(layer, cx + gap, ey, ew, eh, eye, LV_OPA_COVER);

    /* speaking: waveform mouth driven by the playback level */
    if (av->mouth_on) {
        int32_t my = cy + (int32_t)(f->mouth_y * R);
        int32_t bw = LV_MAX(4, (int32_t)(0.065f * R)), bg = (int32_t)(0.045f * R);
        for (int k = -2; k <= 2; k++) {
            float wave = 0.55f + 0.45f * sinf(av->speak_ph + k * 1.3f);
            int32_t bh = (int32_t)(0.05f * R + (av->level_cur / 255.0f) * 0.16f * R * wave);
            fill_ellipse(layer, cx + k * (bw + bg), my, bw, LV_MAX(bw, bh), eye, LV_OPA_COVER);
        }
    }

    /* thinking / working: comet of dots orbiting the screen rim (screen-centred) */
    if (av->orbit_on) {
        lv_area_t pa;
        lv_obj_get_coords(lv_obj_get_parent(av->root), &pa);
        int32_t ox = pa.x1 + lv_area_get_width(&pa) / 2;
        int32_t oy = pa.y1 + lv_area_get_height(&pa) / 2;
        int32_t rad = UI_W / 2 - 14;
        for (int k = 0; k < 3; k++) {
            float ang = (av->orbit / 10.0f - k * 22.0f) * PI_F / 180.0f;
            int32_t sz = 14 - k * 3;
            fill_ellipse(layer, ox + (int32_t)(cosf(ang) * rad), oy + (int32_t)(sinf(ang) * rad), sz, sz,
                         c24(av->orbit_rgb), (lv_opa_t)(255 - k * 60));
        }
    }
}

/* ------------------------------------------------------------------ animations */

#define EXEC(name, field)                                   \
    static void name(void *var, int32_t v)                  \
    {                                                       \
        ui_avatar_t *av = var;                              \
        av->field = v;                                      \
        redraw(av);                                         \
    }
EXEC(breath_exec, breath)
EXEC(blink_exec, blink)
EXEC(ripple_exec, ripple)
EXEC(orbit_exec, orbit)
EXEC(morph_exec, morph)
EXEC(glow_exec, glow_ph)

static void shake_exec(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void run(ui_avatar_t *av, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                uint32_t back_ms, uint32_t delay, uint32_t repeat_delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, av);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    if (back_ms) lv_anim_set_reverse_duration(&a, back_ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_delay(&a, repeat_delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    if (path) lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void level_timer_cb(lv_timer_t *tm)
{
    ui_avatar_t *av = lv_timer_get_user_data(tm);
    int32_t d = av->level_target - av->level_cur;
    av->level_cur += d / 3 + (d > 0 ? 1 : (d < 0 ? -1 : 0));
    if (av->level_cur < 0) av->level_cur = 0;
    av->speak_ph += 0.45f;
    if (av->speak_ph > 2 * PI_F * 100) av->speak_ph = 0;
    redraw(av);
}

static void stop_all(ui_avatar_t *av)
{
    lv_anim_delete(av, breath_exec);
    lv_anim_delete(av, blink_exec);
    lv_anim_delete(av, ripple_exec);
    lv_anim_delete(av, orbit_exec);
    lv_anim_delete(av, morph_exec);
    lv_anim_delete(av->root, shake_exec);
    lv_obj_set_style_translate_x(av->root, 0, 0);
    lv_timer_pause(av->level_timer);
    av->breath = av->blink = av->ripple = av->orbit = 0;
    av->level_cur = av->level_target = 0;
    av->orbit_on = av->ripple_on = av->mouth_on = false;
    av->error_tint = false;
    redraw(av);
}

static void start_breath(ui_avatar_t *av, uint32_t ms)
{
    run(av, breath_exec, 0, 1000, ms, ms, 0, 0, lv_anim_path_ease_in_out);
}

static void start_blink(ui_avatar_t *av)
{
    run(av, blink_exec, 0, 1000, 90, 120, 1500, 3800, NULL);
}

static void start_morph(ui_avatar_t *av, uint32_t ms)
{
    if (av->bot.shape == BOT_SHAPE_BLOB || av->bot.shape == BOT_SHAPE_CLOUD) {
        run(av, morph_exec, 0, 3600, ms, 0, 0, 0, NULL);
    }
}

static void start_orbit(ui_avatar_t *av, uint32_t ms, uint32_t rgb)
{
    av->orbit_on = true;
    av->orbit_rgb = rgb;
    run(av, orbit_exec, 0, 3600, ms, 0, 0, 0, NULL);
}

/* ------------------------------------------------------------------ renderer ops */

static void proc_build(ui_avatar_t *av)
{
    lv_obj_set_size(av->root, UI_W, UI_H);
    lv_obj_add_event_cb(av->root, draw_cb, LV_EVENT_DRAW_MAIN, av);
    av->level_timer = lv_timer_create(level_timer_cb, 33, av);
    lv_timer_pause(av->level_timer);
}

static void proc_set_bot(ui_avatar_t *av, const bot_info_t *bot)
{
    av->bot = *bot;
    if (av->bot.shape >= BOT_SHAPE_COUNT) av->bot.shape = BOT_SHAPE_CIRCLE;
    avatar_anim_t cur = av->anim;
    av->anim = (avatar_anim_t)-1;
    av->r->set_anim(av, cur);   /* restart with new colours / shape */
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
        start_morph(av, 9000);
        break;
    case AVATAR_ANIM_LISTENING:
        start_breath(av, 1200);
        av->ripple_on = true;
        run(av, ripple_exec, 0, 1000, 1500, 0, 0, 0, NULL);
        start_morph(av, 4000);
        break;
    case AVATAR_ANIM_THINKING:
        start_orbit(av, 1600, 0xF4F5F7);
        start_blink(av);
        start_morph(av, 6000);
        break;
    case AVATAR_ANIM_WORKING:
        start_orbit(av, 800, av->bot.accent);
        start_breath(av, 900);
        start_morph(av, 3000);
        break;
    case AVATAR_ANIM_SPEAKING:
        av->mouth_on = true;
        lv_timer_resume(av->level_timer);
        start_morph(av, 5000);
        break;
    case AVATAR_ANIM_ERROR: {
        av->error_tint = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, av->root);
        lv_anim_set_exec_cb(&a, shake_exec);
        lv_anim_set_values(&a, -12, 12);
        lv_anim_set_duration(&a, 70);
        lv_anim_set_reverse_duration(&a, 70);
        lv_anim_set_repeat_count(&a, 4);
        lv_anim_start(&a);
        break;
    }
    case AVATAR_ANIM_STATIC:
    default:
        break;
    }
    redraw(av);
}

static void proc_set_level(ui_avatar_t *av, uint8_t level) { av->level_target = level; }

static void proc_set_base_scale(ui_avatar_t *av, int32_t scale_256)
{
    if (scale_256 == av->base_scale) return;
    av->base_scale = scale_256;
    redraw(av);
}

static void proc_set_glow(ui_avatar_t *av, bool on)
{
    if (on == av->glow) return;
    av->glow = on;
    lv_anim_delete(av, glow_exec);
    if (on) run(av, glow_exec, 0, 1000, 1800, 1800, 0, 0, lv_anim_path_ease_in_out);
    redraw(av);
}

const ui_avatar_renderer_t ui_avatar_renderer_procedural = {
    .name = "procedural",
    .build = proc_build,
    .set_bot = proc_set_bot,
    .set_anim = proc_set_anim,
    .set_level = proc_set_level,
    .set_base_scale = proc_set_base_scale,
    .set_glow = proc_set_glow,
};

/* ------------------------------------------------------------------ public */

ui_avatar_t *ui_avatar_create(lv_obj_t *parent, const ui_avatar_renderer_t *renderer)
{
    ui_avatar_t *av = lv_malloc_zeroed(sizeof(*av));
    LV_ASSERT_MALLOC(av);
    av->r = renderer ? renderer : &ui_avatar_renderer_procedural;
    av->base_scale = 256;
    av->anim = AVATAR_ANIM_STATIC;
    av->bot.scale_pct = 86;
    av->bot.wobble = 50;
    av->root = lv_obj_create(parent);
    lv_obj_remove_style_all(av->root);
    lv_obj_remove_flag(av->root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    av->r->build(av);
    return av;
}

lv_obj_t *ui_avatar_obj(ui_avatar_t *av) { return av->root; }
void ui_avatar_set_bot(ui_avatar_t *av, const bot_info_t *bot) { av->r->set_bot(av, bot); }
void ui_avatar_set_anim(ui_avatar_t *av, avatar_anim_t anim) { av->r->set_anim(av, anim); }
void ui_avatar_set_level(ui_avatar_t *av, uint8_t level) { av->r->set_level(av, level); }
void ui_avatar_set_base_scale(ui_avatar_t *av, int32_t s) { av->r->set_base_scale(av, s); }
void ui_avatar_set_glow(ui_avatar_t *av, bool on)
{
    if (av->r->set_glow) av->r->set_glow(av, on);
}

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

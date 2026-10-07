/**
 * UI core: init, thread-safe public API, shared helpers, dim/lock overlay.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

static const char *TAG = "ui";
ui_ctx_t g_ui;

static lv_obj_t *s_lock_overlay;
static lv_obj_t *s_lock_label;

#define LOCK()   bsp_display_lock((uint32_t)-1)   /* -1 = wait forever */
#define UNLOCK() bsp_display_unlock()

/* ------------------------------------------------------------------ helpers */

void ui_post(app_event_type_t type, int32_t ival, int32_t ival2, const char *str)
{
    if (!g_ui.cb) return;
    app_event_t ev = {.type = type, .ival = ival, .ival2 = ival2};
    if (str) strlcpy(ev.str, str, sizeof(ev.str));
    g_ui.cb(&ev);
}

void ui_note_activity(void)
{
    uint32_t now = lv_tick_get();
    if (now - g_ui.last_activity_post_ms > 500) {
        g_ui.last_activity_post_ms = now;
        ui_post(APP_EV_USER_ACTIVITY, 0, 0, NULL);
    }
}

const char *ui_status_default_text(muse_status_t s)
{
    switch (s) {
    case MUSE_STATUS_LISTENING: return "Listening...";
    case MUSE_STATUS_UPLOADING: return "Sending...";
    case MUSE_STATUS_THINKING:  return "Thinking...";
    case MUSE_STATUS_WORKING:   return "Working on it";
    case MUSE_STATUS_SPEAKING:  return "Speaking";
    case MUSE_STATUS_ERROR:     return "Something went wrong";
    case MUSE_STATUS_IDLE:
    default:                    return "Ready";
    }
}

lv_color_t ui_status_color(muse_status_t s)
{
    switch (s) {
    case MUSE_STATUS_LISTENING: return UI_COL_OK;
    case MUSE_STATUS_UPLOADING:
    case MUSE_STATUS_THINKING:  return UI_COL_ACCENT;
    case MUSE_STATUS_WORKING:   return UI_COL_WARN;
    case MUSE_STATUS_SPEAKING:  return lv_color_hex(0xA78BFA);
    case MUSE_STATUS_ERROR:     return UI_COL_ERR;
    default:                    return UI_COL_DIM;
    }
}

lv_obj_t *ui_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt ? txt : "");
    return l;
}

static void slide_exec(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void slide_done_hide(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

void ui_panel_slide(lv_obj_t *panel, bool open, int32_t from_y)
{
    lv_anim_delete(panel, slide_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_exec_cb(&a, slide_exec);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (open) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
        lv_anim_set_values(&a, from_y, 0);
    } else {
        lv_anim_set_values(&a, lv_obj_get_style_translate_y(panel, 0), from_y);
        lv_anim_set_completed_cb(&a, slide_done_hide);
    }
    lv_anim_start(&a);
}

void ui_set_panel(ui_panel_t p)
{
    if (g_ui.panel == p) return;
    g_ui.panel = p;
    ui_post(APP_EV_PANEL, (int32_t)p, 0, NULL);
}

void ui_format_time(char *buf, size_t n, bool with_date)
{
    if (!g_ui.dev.time_valid) {
        strlcpy(buf, "--:--", n);
        return;
    }
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int h12 = lt.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    if (with_date) {
        char date[24];
        strftime(date, sizeof(date), "%a %b %d", &lt);
        snprintf(buf, n, "%d:%02d %s - %s", h12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM", date);
    } else {
        snprintf(buf, n, "%d:%02d", h12, lt.tm_min);
    }
}

const char *ui_battery_symbol(int pct, bool charging)
{
    if (charging) return LV_SYMBOL_CHARGE;
    if (pct < 0) return LV_SYMBOL_BATTERY_EMPTY;
    if (pct > 85) return LV_SYMBOL_BATTERY_FULL;
    if (pct > 60) return LV_SYMBOL_BATTERY_3;
    if (pct > 35) return LV_SYMBOL_BATTERY_2;
    if (pct > 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

const char *ui_voice_name(const char *voice_id)
{
    for (int i = 0; i < g_ui.voice_count; i++) {
        if (strcmp(g_ui.voices[i].id, voice_id) == 0) return g_ui.voices[i].name;
    }
    return (voice_id && voice_id[0]) ? voice_id : "Default";
}

/* ------------------------------------------------------------------ lock overlay */

void ui_lock_overlay_build(void)
{
    /* Opaque: only the hint is visible while the panel is dimmed, right before it goes dark. */
    s_lock_overlay = ui_plain(lv_layer_top());
    lv_obj_set_size(s_lock_overlay, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_lock_overlay, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_lock_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *icon = ui_label(s_lock_overlay, UI_FONT_NAME, UI_COL_MUTED, LV_SYMBOL_EYE_CLOSE);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);
    s_lock_label = ui_label(s_lock_overlay, UI_FONT_BODY, UI_COL_MUTED,
                            "Sleeping\npress BOOT to wake\nhold BOOT to talk");
    lv_obj_set_style_text_align(s_lock_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_lock_label, 6, 0);
    lv_obj_align(s_lock_label, LV_ALIGN_CENTER, 0, 40);
}

/* ------------------------------------------------------------------ init */

static void screen_activity_cb(lv_event_t *e)
{
    (void)e;
    ui_note_activity();
}

esp_err_t ui_init(app_event_cb_t cb)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.cb = cb;
    g_ui.brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    g_ui.dev.battery_pct = -1;

    /* BSP v3: CO5300 QSPI panel + CST9217 touch + LVGL task (esp_lvgl_adapter). */
    g_ui.disp = bsp_display_start();
    if (!g_ui.disp) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }
    g_ui.indev = bsp_display_get_input_dev();
    bsp_display_brightness_set(g_ui.brightness);

    LOCK();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_activity_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);

    ui_home_build(scr);
    ui_bot_panel_build();
    ui_voice_picker_build();
    ui_device_panel_build();
    ui_lock_overlay_build();
    UNLOCK();
    ESP_LOGI(TAG, "UI ready (LVGL %d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
    return ESP_OK;
}

/* ------------------------------------------------------------------ public API */

void ui_set_bots(const bot_info_t *bots, int count, int selected_idx)
{
    if (count > BOT_MAX) count = BOT_MAX;
    LOCK();
    memcpy(g_ui.bots, bots, sizeof(bot_info_t) * count);
    g_ui.bot_count = count;
    g_ui.sel = (selected_idx >= 0 && selected_idx < count) ? selected_idx : 0;
    ui_home_refresh_bots();
    ui_bot_panel_refresh();
    UNLOCK();
}

void ui_set_voices(const voice_info_t *voices, int count, bool placeholder)
{
    if (count > VOICE_MAX) count = VOICE_MAX;
    LOCK();
    memcpy(g_ui.voices, voices, sizeof(voice_info_t) * count);
    g_ui.voice_count = count;
    g_ui.voices_placeholder = placeholder;
    ui_voice_picker_refresh();
    ui_bot_panel_refresh();
    UNLOCK();
}

void ui_set_current_voice(const char *voice_id)
{
    LOCK();
    strlcpy(g_ui.cur_voice, voice_id ? voice_id : "", sizeof(g_ui.cur_voice));
    ui_bot_panel_refresh();
    ui_voice_picker_refresh();
    UNLOCK();
}

void ui_set_state(muse_status_t state, const char *text)
{
    LOCK();
    g_ui.state = state;
    strlcpy(g_ui.status_text, (text && text[0]) ? text : ui_status_default_text(state),
            sizeof(g_ui.status_text));
    ui_home_refresh_status();
    UNLOCK();
}

void ui_set_level(uint8_t level)
{
    LOCK();
    ui_home_set_level(level);
    UNLOCK();
}

void ui_set_level_provider(uint8_t (*fn)(void))
{
    LOCK();
    g_ui.level_fn = fn;
    UNLOCK();
}

void ui_set_settings(const device_settings_t *s)
{
    LOCK();
    g_ui.settings = *s;
    g_ui.brightness = s->brightness;
    ui_bot_panel_refresh();
    ui_home_refresh_status();
    UNLOCK();
}

void ui_set_conversation_active(bool active)
{
    LOCK();
    g_ui.conv_active = active;
    ui_bot_panel_refresh();
    ui_home_refresh_status();
    UNLOCK();
}

void ui_set_turn_active(bool active)
{
    LOCK();
    g_ui.turn_active = active;
    UNLOCK();
}

void ui_set_device_info(const ui_device_info_t *info)
{
    LOCK();
    g_ui.dev = *info;
    if (g_ui.panel == UI_PANEL_DEVICE) ui_device_panel_refresh();
    UNLOCK();
}

void ui_set_brightness(uint8_t pct)
{
    g_ui.brightness = pct;
    if (!g_ui.locked && !g_ui.dimmed) bsp_display_brightness_set(pct);
}

ui_panel_t ui_get_panel(void) { return g_ui.panel; }

void ui_power_dim(void)
{
    LOCK();
    g_ui.dimmed = true;
    lv_obj_remove_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_lock_overlay);
    UNLOCK();
    bsp_display_brightness_set(g_ui.brightness > 20 ? 10 : g_ui.brightness / 2);
}

void ui_power_undim(void)
{
    LOCK();
    g_ui.dimmed = false;
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    UNLOCK();
    bsp_display_brightness_set(g_ui.brightness);
}

void ui_power_lock(void)
{
    LOCK();
    g_ui.locked = true;
    /* Close panels so wake always lands on the last bot's home screen. */
    if (g_ui.panel == UI_PANEL_VOICE_PICKER) ui_voice_picker_close();
    if (g_ui.panel == UI_PANEL_BOT) ui_bot_panel_close();
    if (g_ui.panel == UI_PANEL_DEVICE) ui_device_panel_close();
    if (g_ui.indev) lv_indev_enable(g_ui.indev, false);   /* touch locked */
    UNLOCK();
    bsp_display_backlight_off();
    /* Stop the LVGL worker + its tick timer so tickless idle can reach light sleep. */
    esp_err_t err = esp_lv_adapter_pause(500);
    if (err != ESP_OK) ESP_LOGW(TAG, "LVGL pause: %s", esp_err_to_name(err));
    /* HARDWARE TODO (battery): CO5300 sleep-in (0x10) and gate the AMOLED supply via the
     * panel handle / AXP2101 rail, and put CST9217 into its sleep mode. Measure mA with
     * the AXP2101 once the board is here (target: one charge per day). */
}

void ui_power_unlock(void)
{
    esp_err_t err = esp_lv_adapter_resume();
    if (err != ESP_OK) ESP_LOGW(TAG, "LVGL resume: %s", esp_err_to_name(err));
    LOCK();
    g_ui.locked = false;
    g_ui.dimmed = false;
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    if (g_ui.indev) lv_indev_enable(g_ui.indev, true);
    UNLOCK();
    bsp_display_brightness_set(g_ui.brightness);
}

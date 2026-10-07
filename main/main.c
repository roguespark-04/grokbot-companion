/**
 * Grok Bot Companion — ESP-IDF thin client
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 round AMOLED, CST9217 touch)
 *
 * Buttons (confirmed):
 *   BOOT (GPIO0)  awake: hold-to-talk (press-to-talk mode)
 *                 locked: short press = wake screen only, hold = wake + talk
 *   PWR           power on/off only (AXP2101) — never PTT
 *
 * Touch UI (main/ui/): bot carousel (swipe ←/→, infinite), swipe up from bottom = per-bot
 * panel (talk mode, conversation, voice, volume, brightness, auto-sleep), swipe down from
 * top = device settings. See README.md "Touch UI" + "Sleep & lock".
 *
 * Turn state machine (per selected bot):
 *   IDLE → LISTENING → UPLOADING → THINKING/WORKING (relay status) → SPEAKING → IDLE
 * Capture starts on BOOT hold (press-to-talk, default) or on energy VAD (always-listen,
 * only while a conversation is open).
 *
 * Device talks ONLY to the audio relay. Relay holds Meridian's sender key and wakes
 * Meridian with JSON incl. target_bot_id + voice_id; Meridian routes to the chosen bot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "app_events.h"
#include "audio_capture.h"
#include "audio_playback.h"
#include "bot_registry.h"
#include "muse_state.h"
#include "power_info.h"
#include "power_mgr.h"
#include "ptt_button.h"
#include "rtc_time.h"
#include "settings_store.h"
#include "ui.h"
#include "vad.h"
#include "webhook_client.h"
#include "wifi_net.h"

static const char *TAG = "companion";

#define LAST_BOT_PERSIST_DELAY_MS  1500
#define STATUS_POLL_MS             2000
#define SYS_TICK_MS                1000
#define CATALOG_REFRESH_MS         (15 * 60 * 1000)

typedef struct {
    int            bot_idx;
    char           bot_id[BOT_ID_MAX];
    char           voice_id[VOICE_ID_MAX];
    char           conv_id[24];
    bool           conv_active;
    talk_mode_t    talk_mode;
    bool           turn_active;
    muse_status_t  remote_state;      /* latest GET /status for the selected bot */
    bool           relay_ok;
    int64_t        last_bot_change_us;
    bool           last_bot_dirty;
} app_state_t;

static app_state_t       s_app;
static SemaphoreHandle_t s_mx;
static QueueHandle_t     s_evq;

static void app_lock(void)   { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void app_unlock(void) { xSemaphoreGive(s_mx); }
static int64_t now_ms(void)  { return esp_timer_get_time() / 1000; }

/* ------------------------------------------------------------------ helpers */

/** Built-in LVGL fonts are ASCII + symbols: map common Unicode punctuation, drop the rest. */
static void ascii_sanitize(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 4 < n;) {
        if (*p < 0x80) {
            dst[o++] = (char)*p++;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {          /* … */
            memcpy(dst + o, "...", 3); o += 3; p += 3;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x98 || p[2] == 0x99)) {
            dst[o++] = '\''; p += 3;                                         /* ‘ ’ */
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x9C || p[2] == 0x9D)) {
            dst[o++] = '"'; p += 3;                                          /* “ ” */
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x93 || p[2] == 0x94)) {
            dst[o++] = '-'; p += 3;                                          /* – — */
        } else if (p[0] == 0xC2 && p[1] == 0xB7) {
            dst[o++] = '-'; p += 2;                                          /* · */
        } else {
            p++;
            while ((*p & 0xC0) == 0x80) p++;                                 /* skip */
        }
    }
    dst[o] = '\0';
}

static void ui_event_cb(const app_event_t *ev)
{
    /* LVGL task context: never block here. */
    if (xQueueSend(s_evq, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped %d", ev->type);
    }
}

static void select_bot(int idx, bool from_user)
{
    const bot_info_t *b = bot_registry_get(idx);
    if (!b) return;
    char voice[VOICE_ID_MAX];
    settings_get_voice(b->id, b->default_voice_id, voice, sizeof(voice));
    app_lock();
    bool changed = strcmp(s_app.bot_id, b->id) != 0;
    s_app.bot_idx = idx;
    strlcpy(s_app.bot_id, b->id, sizeof(s_app.bot_id));
    strlcpy(s_app.voice_id, voice, sizeof(s_app.voice_id));
    if (changed) {
        s_app.remote_state = MUSE_STATUS_IDLE;
        if (from_user) {
            s_app.last_bot_dirty = true;
            s_app.last_bot_change_us = esp_timer_get_time();
        }
    }
    app_unlock();
    ui_set_current_voice(voice);
    if (changed) ui_set_state(MUSE_STATUS_IDLE, NULL);
    ESP_LOGI(TAG, "bot → %s (voice %s)", b->id, voice[0] ? voice : "default");
}

static void persist_last_bot_now(void)
{
    char id[BOT_ID_MAX];
    app_lock();
    strlcpy(id, s_app.bot_id, sizeof(id));
    s_app.last_bot_dirty = false;
    app_unlock();
    if (id[0]) settings_set_last_bot(id);
}

static void set_conversation(bool active)
{
    app_lock();
    s_app.conv_active = active;
    if (active) {
        snprintf(s_app.conv_id, sizeof(s_app.conv_id), "c_%08lx", (unsigned long)esp_random());
    } else {
        s_app.conv_id[0] = '\0';
    }
    app_unlock();
    ui_set_conversation_active(active);
    ESP_LOGI(TAG, "conversation %s", active ? "started" : "ended");
}

/* ------------------------------------------------------------------ turn */

typedef enum { CAPTURE_PTT, CAPTURE_VAD } capture_mode_t;

static void fail_turn(const char *msg)
{
    ESP_LOGW(TAG, "turn failed: %s", msg);
    ui_set_state(MUSE_STATUS_ERROR, msg);
    vTaskDelay(pdMS_TO_TICKS(1800));
    ui_set_state(MUSE_STATUS_IDLE, NULL);
}

/**
 * One voice turn for the selected bot.
 * already_recording: capture was started by power_mgr (wake-hold) or VAD (with pre-roll).
 */
static void turn_body(capture_mode_t mode, bool already_recording)
{
    relay_route_t route;
    char bot_id[BOT_ID_MAX], voice_id[VOICE_ID_MAX], conv_id[24];
    app_lock();
    s_app.turn_active = true;
    strlcpy(bot_id, s_app.bot_id, sizeof(bot_id));
    strlcpy(voice_id, s_app.voice_id, sizeof(voice_id));
    strlcpy(conv_id, s_app.conv_id, sizeof(conv_id));
    app_unlock();
    route.target_bot_id = bot_id;
    route.voice_id = voice_id;
    route.conversation_id = conv_id;
    persist_last_bot_now();   /* "last bot he talked with" survives sleep / reboot */

    ui_set_state(MUSE_STATUS_LISTENING, NULL);
    if (!already_recording) audio_capture_start(mode == CAPTURE_VAD);

    int16_t frame[AUDIO_FRAME_SAMPLES];
    const uint32_t max_ms = CONFIG_MUSE_MAX_UTTERANCE_S * 1000;
    for (;;) {
        (void)audio_capture_read(frame, AUDIO_FRAME_SAMPLES);   /* ~20 ms */
        if (mode == CAPTURE_PTT) {
            if (!ptt_button_raw_pressed()) break;
        } else if (vad_process(frame, AUDIO_FRAME_SAMPLES) == VAD_EVENT_SPEECH_END) {
            break;
        }
        if (audio_capture_recorded_ms() >= max_ms) break;
    }
    audio_capture_stop();
    ptt_button_update();
    (void)ptt_button_was_just_released();
    power_mgr_note_activity();

    uint8_t *wav = NULL;
    size_t wav_len = 0;
    uint32_t rec_ms = audio_capture_recorded_ms();
    esp_err_t err = audio_capture_export_wav(&wav, &wav_len);
    if (err != ESP_OK || rec_ms < CONFIG_MUSE_MIN_UTTERANCE_MS) {
        free(wav);
        app_lock(); s_app.turn_active = false; app_unlock();
        if (err == ESP_ERR_INVALID_SIZE) {
            fail_turn("Mic not wired yet (stub)");
        } else {
            ui_set_state(MUSE_STATUS_IDLE, rec_ms ? "Too short - hold to talk" : NULL);
        }
        return;
    }

    ui_set_state(MUSE_STATUS_UPLOADING, NULL);
    webhook_result_t up = {0}, res = {0};
    err = webhook_client_upload_audio(wav, wav_len, &route, &up);
    free(wav);
    app_lock(); s_app.relay_ok = (err == ESP_OK); app_unlock();
    if (err != ESP_OK) {
        app_lock(); s_app.turn_active = false; app_unlock();
        fail_turn(up.http_status ? "Relay rejected the upload" : "Can't reach the relay");
        return;
    }

    /* Poll result; interleave status so the line shows e.g. "Photon is working on it". */
    ui_set_state(MUSE_STATUS_THINKING, NULL);
    relay_result_state_t rs = RELAY_RESULT_PENDING;
    int64_t deadline = now_ms() + (int64_t)CONFIG_MUSE_REPLY_TIMEOUT_S * 1000;
    int iter = 0;
    while (now_ms() < deadline) {
        err = webhook_client_poll_result(&up, &res, &rs);
        if (err == ESP_OK && rs != RELAY_RESULT_PENDING) break;
        if ((iter++ & 1) == 0) {
            relay_status_t st;
            if (webhook_client_get_status(bot_id, &st) == ESP_OK) {
                char txt[121];
                ascii_sanitize(txt, sizeof(txt), st.text);
                muse_status_t shown = (st.state == MUSE_STATUS_WORKING) ? MUSE_STATUS_WORKING
                                                                        : MUSE_STATUS_THINKING;
                ui_set_state(shown, txt[0] ? txt : NULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (rs == RELAY_RESULT_READY && res.reply_audio_url[0]) {
        ui_set_state(MUSE_STATUS_SPEAKING, NULL);
        err = audio_playback_play_url(res.reply_audio_url);
        if (err != ESP_OK) ESP_LOGW(TAG, "playback: %s", esp_err_to_name(err));
        audio_playback_stop();
        ui_set_state(MUSE_STATUS_IDLE, NULL);
    } else if (rs == RELAY_RESULT_ERROR) {
        app_lock(); s_app.turn_active = false; app_unlock();
        fail_turn(res.error[0] ? res.error : "Meridian reported an error");
        return;
    } else if (rs == RELAY_RESULT_READY) {
        ui_set_state(MUSE_STATUS_IDLE, "Replied (no audio)");
    } else {
        app_lock(); s_app.turn_active = false; app_unlock();
        fail_turn("No reply yet - try again");
        return;
    }
    app_lock(); s_app.turn_active = false; app_unlock();
    power_mgr_note_activity();
}

static void run_turn(capture_mode_t mode, bool already_recording)
{
    ui_set_turn_active(true);
    turn_body(mode, already_recording);
    app_lock(); s_app.turn_active = false; app_unlock();
    ui_set_turn_active(false);
}

/* ------------------------------------------------------------------ UI events */

static void handle_event(const app_event_t *ev)
{
    switch (ev->type) {
    case APP_EV_BOT_SELECTED: {
        int idx = bot_registry_index_of(ev->str);
        if (idx >= 0) select_bot(idx, true);
        break;
    }
    case APP_EV_TALK_MODE:
        settings_set_talk_mode((talk_mode_t)ev->ival);
        app_lock(); s_app.talk_mode = (talk_mode_t)ev->ival; app_unlock();
        if (ev->ival == TALK_MODE_PTT) audio_capture_arm(false);
        break;
    case APP_EV_CONVERSATION_TOGGLE: {
        app_lock(); bool on = !s_app.conv_active; app_unlock();
        set_conversation(on);
        break;
    }
    case APP_EV_VOICE_SELECTED: {
        char bot[BOT_ID_MAX];
        app_lock();
        strlcpy(bot, s_app.bot_id, sizeof(bot));
        strlcpy(s_app.voice_id, ev->str, sizeof(s_app.voice_id));
        app_unlock();
        settings_set_voice(bot, ev->str);
        ui_set_current_voice(ev->str);
        break;
    }
    case APP_EV_VOICE_PREVIEW: {
        /* Optional relay clip GET /voices/<id>/sample.wav (TODO: generate with xAI grok-tts).
         * The picker only enables Preview when /voices lists a sample_path. */
        app_lock(); bool busy = s_app.turn_active; app_unlock();
        if (busy) break;
        char url[160];
        snprintf(url, sizeof(url), "/voices/%s/sample.wav", ev->str);
        char full[224];
        webhook_client_relay_url(url, full, sizeof(full));
        esp_err_t e = audio_playback_play_url(full);
        if (e != ESP_OK) ESP_LOGW(TAG, "voice preview %s: %s", ev->str, esp_err_to_name(e));
        audio_playback_stop();
        break;
    }
    case APP_EV_VOLUME:
        audio_playback_set_volume((uint8_t)ev->ival);
        if (ev->ival2) settings_set_volume((uint8_t)ev->ival);
        break;
    case APP_EV_BRIGHTNESS:
        if (ev->ival2) settings_set_brightness((uint8_t)ev->ival);
        break;
    case APP_EV_SLEEP_TIMEOUT:
        settings_set_sleep_s((uint16_t)ev->ival);
        power_mgr_set_timeout_s((uint16_t)ev->ival);
        break;
    case APP_EV_USER_ACTIVITY:
    case APP_EV_PANEL:
        power_mgr_note_activity();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ system task */

static void refresh_catalogs(void)
{
    char *json = NULL;
    size_t len = 0;
    bool changed = false;
    if (webhook_client_get_json("bots", &json, &len) == ESP_OK) {
        if (bot_registry_update_from_json(json, len, true, &changed) == ESP_OK && changed) {
            app_lock();
            char cur[BOT_ID_MAX];
            strlcpy(cur, s_app.bot_id, sizeof(cur));
            app_unlock();
            int idx = bot_registry_index_of(cur);
            if (idx < 0) idx = bot_registry_index_of(bot_registry_default_id());
            if (idx < 0) idx = 0;
            ui_set_bots(bot_registry_all(), bot_registry_count(), idx);
            select_bot(idx, false);
        }
        free(json);
        json = NULL;
    }
    if (webhook_client_get_json("voices", &json, &len) == ESP_OK) {
        if (voice_registry_update_from_json(json, len, true, &changed) == ESP_OK && changed) {
            ui_set_voices(voice_registry_all(), voice_registry_count(),
                          voice_registry_is_placeholder());
        }
        free(json);
    }
}

static void sys_task(void *arg)
{
    (void)arg;
    bool was_connected = false;
    int64_t last_catalog = -CATALOG_REFRESH_MS, last_status = 0, last_power = -10000;
    power_status_t pwr = {.percent = -1};
    ui_device_info_t info = {0};
    const esp_app_desc_t *desc = esp_app_get_description();
    strlcpy(info.fw_version, desc->version, sizeof(info.fw_version));
    strlcpy(info.device_id, CONFIG_MUSE_DEVICE_ID, sizeof(info.device_id));
    strlcpy(info.relay_url, CONFIG_MUSE_UPLOAD_URL, sizeof(info.relay_url));

    for (;;) {
        if (power_mgr_is_locked()) {
            /* Screen off: stay quiet so tickless idle can light-sleep. */
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        int64_t now = now_ms();
        bool connected = wifi_net_is_connected();
        if (connected && !was_connected) {
            rtc_time_start_sntp();
            last_catalog = now - CATALOG_REFRESH_MS;   /* fetch now */
        }
        was_connected = connected;

        if (connected && now - last_catalog >= CATALOG_REFRESH_MS) {
            refresh_catalogs();
            last_catalog = now;
        }

        /* Live status for the selected bot while a conversation is open (and between turns). */
        app_lock();
        bool poll = s_app.conv_active && !s_app.turn_active;
        char bot[BOT_ID_MAX];
        strlcpy(bot, s_app.bot_id, sizeof(bot));
        app_unlock();
        if (connected && poll && now - last_status >= STATUS_POLL_MS) {
            last_status = now;
            relay_status_t st;
            esp_err_t err = webhook_client_get_status(bot, &st);
            app_lock();
            s_app.relay_ok = (err == ESP_OK);
            bool still = !s_app.turn_active && strcmp(bot, s_app.bot_id) == 0;
            if (err == ESP_OK && still) s_app.remote_state = st.state;
            app_unlock();
            if (err == ESP_OK && still) {
                char txt[121];
                ascii_sanitize(txt, sizeof(txt), st.text);
                ui_set_state(st.state, txt[0] ? txt : NULL);
            }
        }

        if (now - last_power >= 10000) {
            power_info_read(&pwr);
            last_power = now;
        }
        wifi_info_t wi;
        wifi_net_get_info(&wi);
        strlcpy(info.wifi_ssid, wi.ssid, sizeof(info.wifi_ssid));
        strlcpy(info.wifi_state, wifi_state_str(wi.state), sizeof(info.wifi_state));
        strlcpy(info.wifi_ip, wi.ip, sizeof(info.wifi_ip));
        info.wifi_rssi = wi.rssi;
        info.wifi_connected = (wi.state == WIFI_STATE_CONNECTED);
        info.battery_pct = pwr.valid ? pwr.percent : -1;
        info.charging = pwr.charging;
        info.usb_power = pwr.vbus_present;
        info.battery_mv = pwr.battery_mv;
        info.time_valid = rtc_time_valid();
        strlcpy(info.time_source, rtc_time_source_str(), sizeof(info.time_source));
        app_lock(); info.relay_ok = s_app.relay_ok; app_unlock();
        ui_set_device_info(&info);

        vTaskDelay(pdMS_TO_TICKS(SYS_TICK_MS));
    }
}

/* ------------------------------------------------------------------ app_main */

static const power_mgr_hooks_t PM_HOOKS = {
    .dim = ui_power_dim,
    .undim = ui_power_undim,
    .lock = ui_power_lock,
    .unlock = ui_power_unlock,
};

void app_main(void)
{
    ESP_LOGI(TAG, "Grok Bot Companion %s — device_id=%s", esp_app_get_description()->version,
             CONFIG_MUSE_DEVICE_ID);
    s_mx = xSemaphoreCreateMutex();
    s_evq = xQueueCreate(16, sizeof(app_event_t));

    ESP_ERROR_CHECK(settings_init());
    device_settings_t cfg;
    settings_get(&cfg);
    bot_registry_init();

    ESP_ERROR_CHECK(ui_init(ui_event_cb));
    ui_set_settings(&cfg);
    ui_set_voices(voice_registry_all(), voice_registry_count(), voice_registry_is_placeholder());
    ui_set_level_provider(audio_playback_level);

    /* Back to the last bot (NVS), else the roster default. */
    int idx = bot_registry_index_of(cfg.last_bot_id);
    if (idx < 0) idx = bot_registry_index_of(bot_registry_default_id());
    if (idx < 0) idx = 0;
    ui_set_bots(bot_registry_all(), bot_registry_count(), idx);
    s_app.talk_mode = cfg.talk_mode;
    select_bot(idx, false);
    ui_set_state(MUSE_STATUS_IDLE, NULL);
    ui_set_brightness(cfg.brightness);

    ESP_ERROR_CHECK(ptt_button_init());
    ESP_ERROR_CHECK(audio_capture_init());
    ESP_ERROR_CHECK(audio_playback_init());
    audio_playback_set_volume(cfg.volume);
    if (power_info_init() != ESP_OK) ESP_LOGW(TAG, "AXP2101 init failed");
    if (rtc_time_init() != ESP_OK) ESP_LOGW(TAG, "RTC init failed");
    ESP_ERROR_CHECK(wifi_net_init());
    ESP_ERROR_CHECK(webhook_client_init());
    (void)wifi_net_connect();

    vad_config_t vcfg = VAD_CONFIG_DEFAULT();
    vcfg.sample_rate = CONFIG_MUSE_AUDIO_SAMPLE_RATE;
    vad_init(&vcfg);
    power_mgr_init(&PM_HOOKS, cfg.sleep_s);

    xTaskCreatePinnedToCore(sys_task, "sys", 6144, NULL, 3, NULL, 0);

    int16_t frame[AUDIO_FRAME_SAMPLES];
    for (;;) {
        app_event_t ev;
        while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) handle_event(&ev);

        app_lock();
        bool always = s_app.talk_mode == TALK_MODE_ALWAYS && s_app.conv_active;
        bool busy = s_app.turn_active || always || muse_status_is_busy(s_app.remote_state);
        bool dirty = s_app.last_bot_dirty &&
                     (esp_timer_get_time() - s_app.last_bot_change_us) / 1000 > LAST_BOT_PERSIST_DELAY_MS;
        app_unlock();
        if (dirty) persist_last_bot_now();

        power_mgr_set_busy(busy);
        pm_wake_t wake = power_mgr_tick();          /* may block while locked */
        if (wake == PM_WAKE_HOLD) {
            run_turn(CAPTURE_PTT, true);            /* recording since wake */
            continue;
        }

        /* Press-to-talk (works in both talk modes). */
        if (ptt_button_was_just_pressed()) {
            power_mgr_note_activity();
            vTaskDelay(pdMS_TO_TICKS(CONFIG_MUSE_PTT_HOLD_MS));
            if (ptt_button_raw_pressed()) {
                vad_reset();
                run_turn(CAPTURE_PTT, false);
                continue;
            }
        }
        (void)ptt_button_was_just_released();

        /* Always-listen: mic armed, energy VAD triggers a turn (pre-roll included). */
        if (always && !power_mgr_is_locked()) {
            if (!audio_capture_is_armed()) {
                audio_capture_arm(true);
                vad_reset();
            }
            size_t n = audio_capture_read(frame, AUDIO_FRAME_SAMPLES);   /* ~20 ms */
            if (n && vad_process(frame, n) == VAD_EVENT_SPEECH_START) {
                audio_capture_start(true);
                run_turn(CAPTURE_VAD, true);
                vad_reset();
            }
        } else {
            if (audio_capture_is_armed() && !always) audio_capture_arm(false);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

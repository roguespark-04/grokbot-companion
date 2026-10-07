/**
 * Idle sleep / lock state machine — see power_mgr.h.
 */
#include "power_mgr.h"

#include "audio_capture.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ptt_button.h"
#include "sdkconfig.h"
#include "wifi_net.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "power_mgr";

static power_mgr_hooks_t s_hooks;
static volatile pm_state_t s_state = PM_STATE_AWAKE;
static volatile uint16_t s_timeout_s;
static volatile bool s_busy;
static volatile int64_t s_last_activity_us;
static volatile bool s_activity_flag;
static int64_t s_dim_start_us;

static int64_t now_us(void) { return esp_timer_get_time(); }

static void pm_light_sleep(bool allow)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = allow ? 40 : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = allow,
    };
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) ESP_LOGW(TAG, "esp_pm_configure: %s", esp_err_to_name(err));
#else
    (void)allow;
#endif
}

esp_err_t power_mgr_init(const power_mgr_hooks_t *hooks, uint16_t timeout_s)
{
    if (hooks) s_hooks = *hooks;
    s_timeout_s = timeout_s;
    s_last_activity_us = now_us();
    pm_light_sleep(false);
    ESP_LOGI(TAG, "auto-sleep %us, long-press threshold %d ms", timeout_s,
             CONFIG_MUSE_WAKE_LONG_PRESS_MS);
    return ESP_OK;
}

void power_mgr_set_timeout_s(uint16_t timeout_s)
{
    s_timeout_s = timeout_s;
    s_last_activity_us = now_us();
}

void power_mgr_set_busy(bool busy) { s_busy = busy; }

void power_mgr_note_activity(void)
{
    s_last_activity_us = now_us();
    s_activity_flag = true;
}

pm_state_t power_mgr_state(void) { return s_state; }
bool power_mgr_is_locked(void) { return s_state == PM_STATE_LOCKED; }

/** Locked: block in (auto) light sleep until BOOT, then classify short vs hold. */
static pm_wake_t lock_and_wait(void)
{
    ESP_LOGI(TAG, "→ LOCKED (screen off, touch locked, light sleep; BOOT wakes)");
    s_state = PM_STATE_LOCKED;
    if (s_hooks.lock) s_hooks.lock();
    audio_capture_arm(false);
    wifi_net_set_low_power(true);
    ptt_button_enable_wake(true);
    pm_light_sleep(true);

    /* Wait for BOOT. Periodic wakeups are not needed; Wi-Fi modem sleep keeps the
     * association on DTIM beacons on its own. */
    while (!ptt_button_wait_wake(UINT32_MAX)) {
    }

    /* ---- woken: capture first, ask questions later ---- */
    pm_light_sleep(false);
    ptt_button_enable_wake(false);
    int64_t t0 = now_us();
    audio_capture_start(false);      /* pre-arm ES7210 + record from the first frame */
    wifi_net_set_low_power(false);
    if (s_hooks.unlock) s_hooks.unlock();   /* last bot is still the selected one */
    s_state = PM_STATE_AWAKE;
    s_last_activity_us = now_us();

    int16_t frame[AUDIO_FRAME_SAMPLES];
    const int64_t long_us = (int64_t)CONFIG_MUSE_WAKE_LONG_PRESS_MS * 1000;
    bool held = ptt_button_raw_pressed();
    while (held && (now_us() - t0) < long_us) {
        (void)audio_capture_read(frame, AUDIO_FRAME_SAMPLES);   /* ~20 ms, keeps buffering */
        held = ptt_button_raw_pressed();
    }
    ptt_button_consume_until_release();     /* this press never becomes a PTT edge */

    if (held) {
        ESP_LOGI(TAG, "wake: HOLD → voice turn (recording since wake)");
        return PM_WAKE_HOLD;
    }
    audio_capture_discard();
    audio_capture_arm(false);
    ESP_LOGI(TAG, "wake: short press → screen on, no recording");
    return PM_WAKE_SHORT;
}

pm_wake_t power_mgr_tick(void)
{
    int64_t now = now_us();
    if (s_busy) {
        s_last_activity_us = now;   /* idle timer starts when the bot goes quiet */
    }
    bool activity = s_activity_flag;
    s_activity_flag = false;

    switch (s_state) {
    case PM_STATE_AWAKE:
        if (s_timeout_s && !s_busy &&
            now - s_last_activity_us >= (int64_t)s_timeout_s * 1000000) {
            ESP_LOGI(TAG, "→ DIMMED");
            s_state = PM_STATE_DIMMED;
            s_dim_start_us = now;
            if (s_hooks.dim) s_hooks.dim();
        }
        break;
    case PM_STATE_DIMMED:
        if (activity || s_busy || s_timeout_s == 0) {
            ESP_LOGI(TAG, "→ AWAKE (activity)");
            s_state = PM_STATE_AWAKE;
            s_last_activity_us = now;
            if (s_hooks.undim) s_hooks.undim();
        } else if (now - s_dim_start_us >= (int64_t)CONFIG_MUSE_DIM_BEFORE_SLEEP_MS * 1000) {
            return lock_and_wait();
        }
        break;
    case PM_STATE_LOCKED:
    default:
        break;
    }
    return PM_WAKE_NONE;
}

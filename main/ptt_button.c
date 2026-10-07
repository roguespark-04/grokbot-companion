/**
 * STUB — BOOT (GPIO0) press-and-hold PTT, active low.
 *
 * Confirmed button mapping (Frank):
 *   BOOT (GPIO0) = PTT: hold = capture; release = stop capture → upload→relay
 *   PWR          = power on/off only (AXP2101 / board PMIC) — not used here
 */
#include "ptt_button.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ptt_button";
static bool s_prev = false;
static bool s_pressed = false;
static bool s_just_pressed = false;
static bool s_just_released = false;

esp_err_t ptt_button_init(void)
{
#if CONFIG_MUSE_PTT_USE_BOOT_BUTTON
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MUSE_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG, "PTT = BOOT GPIO%d (active low; hold=capture, release=upload)",
             (int)MUSE_BOOT_BUTTON);
#else
    ESP_LOGW(TAG, "BOOT PTT disabled in Kconfig — PWR is power-only, not PTT");
#endif
    s_prev = false;
    s_pressed = false;
    return ESP_OK;
}

static void ptt_poll(void)
{
#if CONFIG_MUSE_PTT_USE_BOOT_BUTTON
    /* active low while BOOT held */
    bool now = (gpio_get_level(MUSE_BOOT_BUTTON) == 0);
#else
    bool now = false;
#endif
    s_just_pressed = (now && !s_prev);
    s_just_released = (!now && s_prev);
    s_prev = now;
    s_pressed = now;
}

bool ptt_button_is_pressed(void)
{
    ptt_poll();
    return s_pressed;
}

bool ptt_button_was_just_pressed(void)
{
    ptt_poll();
    return s_just_pressed;
}

bool ptt_button_was_just_released(void)
{
    ptt_poll();
    return s_just_released;
}

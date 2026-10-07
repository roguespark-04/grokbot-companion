/**
 * BOOT (GPIO0) PTT + light-sleep wake source.
 */
#include "ptt_button.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "ptt_button";

static bool s_raw_prev;
static bool s_stable;
static bool s_edge_down;
static bool s_edge_up;
static bool s_suppress;
static SemaphoreHandle_t s_wake_sem;
static bool s_isr_installed;

static void IRAM_ATTR boot_isr(void *arg)
{
    (void)arg;
    /* Level-low interrupt fires continuously while held: disable until re-armed. */
    gpio_intr_disable(MUSE_BOOT_BUTTON);
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_wake_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

esp_err_t ptt_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MUSE_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    s_wake_sem = xSemaphoreCreateBinary();
#if CONFIG_MUSE_PTT_USE_BOOT_BUTTON
    ESP_LOGI(TAG, "BOOT GPIO%d: hold=talk; while locked short=wake, hold=wake+talk",
             (int)MUSE_BOOT_BUTTON);
#else
    ESP_LOGW(TAG, "BOOT PTT disabled in Kconfig — PWR is power-only, not PTT");
#endif
    return ESP_OK;
}

bool ptt_button_raw_pressed(void)
{
#if CONFIG_MUSE_PTT_USE_BOOT_BUTTON
    return gpio_get_level(MUSE_BOOT_BUTTON) == 0;
#else
    return false;
#endif
}

void ptt_button_update(void)
{
    bool raw = ptt_button_raw_pressed();
    /* 2-sample debounce: accept a level once it repeats on consecutive updates. */
    if (raw == s_raw_prev && raw != s_stable) {
        s_stable = raw;
        if (raw) {
            if (!s_suppress) s_edge_down = true;
        } else {
            s_edge_up = true;
            s_suppress = false;
        }
    }
    s_raw_prev = raw;
}

bool ptt_button_is_pressed(void)
{
    ptt_button_update();
    return s_stable;
}

bool ptt_button_was_just_pressed(void)
{
    ptt_button_update();
    bool e = s_edge_down;
    s_edge_down = false;
    return e;
}

bool ptt_button_was_just_released(void)
{
    ptt_button_update();
    bool e = s_edge_up;
    s_edge_up = false;
    return e;
}

void ptt_button_consume_until_release(void)
{
    s_suppress = true;
    s_edge_down = false;
    s_edge_up = false;
    /* If it's already released, the next update clears suppress via the up-edge;
     * make the debounced state match reality so that edge actually happens. */
    s_stable = true;
    s_raw_prev = ptt_button_raw_pressed();
}

esp_err_t ptt_button_enable_wake(bool enable)
{
    if (enable) {
        if (!s_isr_installed) {
            esp_err_t err = gpio_install_isr_service(0);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
            ESP_ERROR_CHECK(gpio_isr_handler_add(MUSE_BOOT_BUTTON, boot_isr, NULL));
            s_isr_installed = true;
        }
        xSemaphoreTake(s_wake_sem, 0);   /* drop stale gives */
        /* Low level wakes the chip from light sleep and fires boot_isr. */
        ESP_ERROR_CHECK(gpio_wakeup_enable(MUSE_BOOT_BUTTON, GPIO_INTR_LOW_LEVEL));
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        gpio_intr_enable(MUSE_BOOT_BUTTON);
    } else {
        gpio_intr_disable(MUSE_BOOT_BUTTON);
        gpio_wakeup_disable(MUSE_BOOT_BUTTON);
        gpio_set_intr_type(MUSE_BOOT_BUTTON, GPIO_INTR_DISABLE);
    }
    return ESP_OK;
}

bool ptt_button_wait_wake(uint32_t timeout_ms)
{
    TickType_t t = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_wake_sem, t) == pdTRUE;
}

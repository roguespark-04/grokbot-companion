/**
 * STUB — status face on 466×466 CO5300 AMOLED (QSPI).
 * TODO: paste Waveshare ESP-IDF 05_LVGL_WITH_RAM display bring-up here.
 * Brightness via CO5300 commands (no backlight GPIO).
 */
#include "display_face.h"
#include "esp_log.h"
#include "board_pins.h"

static const char *TAG = "display_face";
static muse_status_t s_status = MUSE_STATUS_IDLE;

esp_err_t display_face_init(void)
{
    ESP_LOGI(TAG, "stub init %dx%d CO5300 QSPI (CS=%d PCLK=%d)",
             MUSE_LCD_WIDTH, MUSE_LCD_HEIGHT,
             (int)MUSE_LCD_CS, (int)MUSE_LCD_PCLK);
    s_status = MUSE_STATUS_IDLE;
    return ESP_OK;
}

esp_err_t display_face_set_status(muse_status_t status)
{
    s_status = status;
    ESP_LOGI(TAG, "face → %s", muse_status_str(status));
    /* TODO: draw simple color/glyph for idle|listening|thinking|speaking|error */
    return ESP_OK;
}

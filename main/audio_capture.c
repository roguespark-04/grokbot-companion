/**
 * STUB — ES7210 mic capture over I2S TDM.
 * TODO: paste Waveshare ESP-IDF 06_I2SCodec / BSP microphone init here.
 * See board_pins.h and PINOUT.md.
 */
#include "audio_capture.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "audio_capture";

esp_err_t audio_capture_init(void)
{
    ESP_LOGI(TAG, "stub init (ES7210 @0x40, I2S DIN GPIO%d, rate=%d)",
             10, CONFIG_MUSE_AUDIO_SAMPLE_RATE);
    /* TODO: i2c_master + es7210 + i2s_tdm_rx on shared MCLK/BCLK/LRCK */
    return ESP_OK;
}

esp_err_t audio_capture_start(void)
{
    ESP_LOGI(TAG, "stub start");
    return ESP_OK;
}

esp_err_t audio_capture_stop(void)
{
    ESP_LOGI(TAG, "stub stop");
    return ESP_OK;
}

size_t audio_capture_read(int16_t *buf, size_t max_samples)
{
    (void)buf;
    (void)max_samples;
    return 0;
}

esp_err_t audio_capture_export_wav(uint8_t **out_wav, size_t *out_len)
{
    if (out_wav) {
        *out_wav = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    ESP_LOGW(TAG, "export_wav not implemented — wire ES7210 buffer + WAV header");
    return ESP_ERR_NOT_SUPPORTED;
}

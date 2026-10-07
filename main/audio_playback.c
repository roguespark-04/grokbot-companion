/**
 * STUB — ES8311 speaker playback over I2S TX + PA GPIO46.
 * TODO: paste Waveshare 06_I2SCodec / 08_ES8311 / BSP speaker init here.
 */
#include "audio_playback.h"
#include "esp_log.h"

static const char *TAG = "audio_playback";

esp_err_t audio_playback_init(void)
{
    ESP_LOGI(TAG, "stub init (ES8311 @0x18, I2S DOUT GPIO8, PA GPIO46)");
    return ESP_OK;
}

esp_err_t audio_playback_stop(void)
{
    ESP_LOGI(TAG, "stub stop");
    return ESP_OK;
}

esp_err_t audio_playback_play(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "stub play %u bytes", (unsigned)len);
    (void)data;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_playback_play_url(const char *url)
{
    ESP_LOGI(TAG, "stub play_url %s", url ? url : "(null)");
    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * STUB — device ↔ audio relay only (upload + poll).
 * Meridian wake + sender key live on the relay (Spark / shared box).
 */
#include "webhook_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "webhook_client";

esp_err_t webhook_client_init(void)
{
    ESP_LOGI(TAG, "relay base (upload)=%s device=%s",
             CONFIG_MUSE_UPLOAD_URL, CONFIG_MUSE_DEVICE_ID);
    ESP_LOGI(TAG, "Meridian sender key MUST stay on relay — not on ESP32");
    return ESP_OK;
}

esp_err_t webhook_client_upload_audio(const uint8_t *wav, size_t wav_len,
                                      webhook_result_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    ESP_LOGI(TAG, "stub POST %s/upload (%u bytes audio/wav)",
             CONFIG_MUSE_UPLOAD_URL, (unsigned)wav_len);
    (void)wav;
    /* TODO: HTTPS POST with device↔relay auth (HMAC or bearer).
     * Expect JSON: { "job_id", "audio_url", "result_url" }
     * Relay then POSTs JSON wake to Meridian (action=voice_turn).
     */
    snprintf(out->job_id, sizeof(out->job_id), "stub-job");
    snprintf(out->audio_url, sizeof(out->audio_url),
             "%s/audio/stub-job.wav", CONFIG_MUSE_UPLOAD_URL);
    snprintf(out->result_url, sizeof(out->result_url),
             "%s/result/stub-job", CONFIG_MUSE_UPLOAD_URL);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t webhook_client_wake(const webhook_result_t *upload, webhook_result_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    const char *result_url = (upload && upload->result_url[0])
                                 ? upload->result_url
                                 : "";
    ESP_LOGI(TAG, "stub poll GET %s (job_id=%s) until reply ready",
             result_url,
             (upload && upload->job_id[0]) ? upload->job_id : "?");
    /* TODO: poll loop — 202/pending → sleep; 200 → reply_audio_url or WAV body */
    if (upload) {
        strncpy(out->job_id, upload->job_id, sizeof(out->job_id) - 1);
        strncpy(out->result_url, upload->result_url, sizeof(out->result_url) - 1);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

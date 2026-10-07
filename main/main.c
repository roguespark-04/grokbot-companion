/**
 * Pocket companion (product name TBD) — ESP-IDF thin client
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75
 *
 * Vertical slice state machine:
 *   IDLE → LISTENING → UPLOADING → THINKING → SPEAKING → IDLE
 *
 * Device talks ONLY to the audio relay (upload + poll).
 * Relay holds Meridian sender key and POSTs JSON wake — no sender key on ESP32.
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "muse_state.h"
#include "audio_capture.h"
#include "audio_playback.h"
#include "display_face.h"
#include "wifi_net.h"
#include "webhook_client.h"
#include "ptt_button.h"

static const char *TAG = "pocket_main";

typedef enum {
    ST_IDLE = 0,
    ST_LISTENING,
    ST_UPLOADING,
    ST_THINKING,
    ST_SPEAKING,
    ST_ERROR,
} app_state_t;

static app_state_t s_state = ST_IDLE;

static void set_state(app_state_t st, muse_status_t face)
{
    s_state = st;
    display_face_set_status(face);
    ESP_LOGI(TAG, "state → %s", muse_status_str(face));
}

static void run_turn(void)
{
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    webhook_result_t upload = {0};
    webhook_result_t wake = {0};
    esp_err_t err;

    set_state(ST_LISTENING, MUSE_STATUS_LISTENING);
    audio_capture_start();

    /* Hold PTT: accumulate PCM until release (stub polls button) */
    while (ptt_button_is_pressed()) {
        int16_t scratch[256];
        (void)audio_capture_read(scratch, 256);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    audio_capture_stop();

    err = audio_capture_export_wav(&wav, &wav_len);
    if (err != ESP_OK || !wav || wav_len == 0) {
        ESP_LOGW(TAG, "no wav yet (stub) — leaving turn");
        set_state(ST_IDLE, MUSE_STATUS_IDLE);
        return;
    }

    set_state(ST_UPLOADING, MUSE_STATUS_UPLOADING);
    err = webhook_client_upload_audio(wav, wav_len, &upload);
    free(wav);
    wav = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload failed: %s", esp_err_to_name(err));
        set_state(ST_ERROR, MUSE_STATUS_ERROR);
        vTaskDelay(pdMS_TO_TICKS(1500));
        set_state(ST_IDLE, MUSE_STATUS_IDLE);
        return;
    }

    set_state(ST_THINKING, MUSE_STATUS_THINKING);
    /* Device → relay only. Relay wakes Meridian. Then poll result URL. */
    err = webhook_client_wake(&upload, &wake);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wake/poll path failed: %s", esp_err_to_name(err));
        set_state(ST_ERROR, MUSE_STATUS_ERROR);
        vTaskDelay(pdMS_TO_TICKS(1500));
        set_state(ST_IDLE, MUSE_STATUS_IDLE);
        return;
    }

    set_state(ST_SPEAKING, MUSE_STATUS_SPEAKING);
    if (wake.reply_audio_url[0]) {
        (void)audio_playback_play_url(wake.reply_audio_url);
    } else {
        ESP_LOGW(TAG, "no reply_audio_url — stub end");
    }
    audio_playback_stop();
    set_state(ST_IDLE, MUSE_STATUS_IDLE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "pocket companion skeleton — device_id=%s", CONFIG_MUSE_DEVICE_ID);
    ESP_LOGI(TAG, "board not required for compile; hardware bring-up TBD");

    ESP_ERROR_CHECK(display_face_init());
    ESP_ERROR_CHECK(ptt_button_init());
    ESP_ERROR_CHECK(audio_capture_init());
    ESP_ERROR_CHECK(audio_playback_init());
    ESP_ERROR_CHECK(wifi_net_init());
    ESP_ERROR_CHECK(webhook_client_init());

    set_state(ST_IDLE, MUSE_STATUS_IDLE);

    /* Non-blocking connect attempt (credentials are placeholders) */
    (void)wifi_net_connect();

    while (1) {
        if (s_state == ST_IDLE && ptt_button_was_just_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_MUSE_PTT_HOLD_MS));
            if (ptt_button_is_pressed()) {
                run_turn();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

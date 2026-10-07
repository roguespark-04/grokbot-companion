/**
 * Playback: download is real (relay client); the ES8311 write is a HARDWARE TODO stub.
 * The stub still walks the PCM in 20 ms chunks so the speaking avatar animates with the
 * real reply envelope and the state machine timing matches the audio length.
 */
#include "audio_playback.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "webhook_client.h"

static const char *TAG = "audio_playback";
static volatile uint8_t s_level;
static volatile bool s_stop;
static uint8_t s_volume = 70;

#define MAX_REPLY_BYTES (4 * 1024 * 1024)

esp_err_t audio_playback_init(void)
{
    ESP_LOGI(TAG, "init (ES8311 @0x18, I2S DOUT GPIO8, PA GPIO46) — codec = TODO stub");
    /* HARDWARE TODO: s_spk = bsp_audio_codec_speaker_init(); esp_codec_dev_set_out_vol() */
    return ESP_OK;
}

esp_err_t audio_playback_set_volume(uint8_t pct)
{
    s_volume = pct > 100 ? 100 : pct;
    /* HARDWARE TODO: esp_codec_dev_set_out_vol(s_spk, s_volume); */
    return ESP_OK;
}

esp_err_t audio_playback_stop(void)
{
    s_stop = true;
    s_level = 0;
    return ESP_OK;
}

/** Minimal RIFF walk → pointer to PCM data chunk. */
static const int16_t *wav_pcm(const uint8_t *wav, size_t len, size_t *samples, uint32_t *rate)
{
    if (len < 44 || memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4)) return NULL;
    size_t off = 12;
    *rate = 16000;
    while (off + 8 <= len) {
        uint32_t clen = wav[off + 4] | (wav[off + 5] << 8) | (wav[off + 6] << 16) | ((uint32_t)wav[off + 7] << 24);
        if (!memcmp(wav + off, "fmt ", 4) && off + 16 <= len) {
            *rate = wav[off + 12] | (wav[off + 13] << 8) | (wav[off + 14] << 16) | ((uint32_t)wav[off + 15] << 24);
        } else if (!memcmp(wav + off, "data", 4)) {
            size_t avail = len - off - 8;
            *samples = (clen < avail ? clen : avail) / 2;
            return (const int16_t *)(wav + off + 8);
        }
        off += 8 + clen + (clen & 1);
    }
    return NULL;
}

esp_err_t audio_playback_play(const uint8_t *wav, size_t len)
{
    size_t n = 0;
    uint32_t rate = 16000;
    const int16_t *pcm = wav_pcm(wav, len, &n, &rate);
    if (!pcm || rate == 0) return ESP_ERR_INVALID_ARG;
    s_stop = false;
    const size_t chunk = rate / 50;   /* 20 ms */
    for (size_t i = 0; i < n && !s_stop; i += chunk) {
        size_t m = (n - i) < chunk ? (n - i) : chunk;
        double acc = 0;
        for (size_t k = 0; k < m; k++) acc += (double)pcm[i + k] * pcm[i + k];
        float rms = (float)sqrt(acc / (double)(m ? m : 1));
        float lvl = rms / 32.0f * s_volume / 100.0f;
        s_level = (uint8_t)(lvl > 255 ? 255 : lvl);
        /* HARDWARE TODO: esp_codec_dev_write(s_spk, &pcm[i], m * 2) — blocks for 20 ms */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_level = 0;
    return ESP_OK;
}

esp_err_t audio_playback_play_url(const char *url)
{
    if (!url || !url[0]) return ESP_ERR_INVALID_ARG;
    uint8_t *wav = NULL;
    size_t len = 0;
    esp_err_t err = webhook_client_download(url, &wav, &len, MAX_REPLY_BYTES);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "reply %u bytes", (unsigned)len);
    err = audio_playback_play(wav, len);
    free(wav);
    return err;
}

uint8_t audio_playback_level(void) { return s_level; }

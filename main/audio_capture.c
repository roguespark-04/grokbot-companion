/**
 * Capture buffers + WAV export are real; the I2S/ES7210 read is a HARDWARE TODO stub.
 * With CONFIG_MUSE_CAPTURE_STUB_SILENCE=y the stub returns silent frames so the full
 * upload → relay → Meridian path can be exercised before the mic is wired.
 */
#include "audio_capture.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "audio_capture";

#define RATE          CONFIG_MUSE_AUDIO_SAMPLE_RATE
#define PREROLL_SAMP  (RATE * CONFIG_MUSE_PREROLL_MS / 1000)
#define MAX_SAMP      (RATE * CONFIG_MUSE_MAX_UTTERANCE_S)

static int16_t *s_turn;      /* PSRAM */
static size_t   s_turn_len;
static int16_t *s_ring;      /* pre-roll ring */
static size_t   s_ring_pos;
static size_t   s_ring_fill;
static bool     s_armed;
static bool     s_recording;

esp_err_t audio_capture_init(void)
{
    s_turn = heap_caps_malloc(MAX_SAMP * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_ring = heap_caps_malloc(PREROLL_SAMP * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_turn || !s_ring) {
        ESP_LOGE(TAG, "PSRAM alloc failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "init: %d Hz, max %d s, pre-roll %d ms (ES7210 I2S = TODO stub)",
             RATE, CONFIG_MUSE_MAX_UTTERANCE_S, CONFIG_MUSE_PREROLL_MS);
    /* HARDWARE TODO: bsp_audio_codec_microphone_init(); esp_codec_dev_open(16k, 2ch TDM) */
    return ESP_OK;
}

esp_err_t audio_capture_arm(bool on)
{
    if (on == s_armed) return ESP_OK;
    s_armed = on;
    if (!on) {
        s_recording = false;
        s_ring_fill = 0;
    }
    /* HARDWARE TODO: esp_codec_dev_open()/close() + I2S RX enable; keep ES7210 powered
     * while armed so a wake-from-sleep long press captures from the first frame. */
    ESP_LOGD(TAG, "mic %s", on ? "armed" : "off");
    return ESP_OK;
}

bool audio_capture_is_armed(void) { return s_armed; }

esp_err_t audio_capture_start(bool with_preroll)
{
    audio_capture_arm(true);
    s_turn_len = 0;
    if (with_preroll && s_ring_fill) {
        size_t start = (s_ring_pos + PREROLL_SAMP - s_ring_fill) % PREROLL_SAMP;
        for (size_t i = 0; i < s_ring_fill; i++) {
            s_turn[s_turn_len++] = s_ring[(start + i) % PREROLL_SAMP];
        }
    }
    s_recording = true;
    return ESP_OK;
}

esp_err_t audio_capture_stop(void)
{
    s_recording = false;
    return ESP_OK;
}

void audio_capture_discard(void)
{
    s_recording = false;
    s_turn_len = 0;
}

static size_t i2s_read_frame(int16_t *buf, size_t max)
{
    /* HARDWARE TODO: esp_codec_dev_read() TDM frame → pick/average MIC1+MIC2 → mono. */
#if CONFIG_MUSE_CAPTURE_STUB_SILENCE
    vTaskDelay(pdMS_TO_TICKS(20));
    size_t n = max < AUDIO_FRAME_SAMPLES ? max : AUDIO_FRAME_SAMPLES;
    memset(buf, 0, n * sizeof(int16_t));
    return n;
#else
    (void)buf;
    (void)max;
    vTaskDelay(pdMS_TO_TICKS(20));
    return 0;
#endif
}

size_t audio_capture_read(int16_t *buf, size_t max_samples)
{
    if (!s_armed || !buf || !max_samples) return 0;
    size_t n = i2s_read_frame(buf, max_samples);
    for (size_t i = 0; i < n; i++) {
        s_ring[s_ring_pos] = buf[i];
        s_ring_pos = (s_ring_pos + 1) % PREROLL_SAMP;
    }
    s_ring_fill = (s_ring_fill + n > PREROLL_SAMP) ? PREROLL_SAMP : s_ring_fill + n;
    if (s_recording) {
        size_t room = MAX_SAMP - s_turn_len;
        size_t take = n < room ? n : room;
        memcpy(&s_turn[s_turn_len], buf, take * sizeof(int16_t));
        s_turn_len += take;
    }
    return n;
}

uint32_t audio_capture_recorded_ms(void)
{
    return (uint32_t)((uint64_t)s_turn_len * 1000 / RATE);
}

static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

esp_err_t audio_capture_export_wav(uint8_t **out_wav, size_t *out_len)
{
    if (!out_wav || !out_len) return ESP_ERR_INVALID_ARG;
    *out_wav = NULL;
    *out_len = 0;
    if (s_turn_len == 0) return ESP_ERR_INVALID_SIZE;
    uint32_t data_len = (uint32_t)(s_turn_len * sizeof(int16_t));
    uint8_t *w = heap_caps_malloc(44 + data_len, MALLOC_CAP_SPIRAM);
    if (!w) return ESP_ERR_NO_MEM;
    memcpy(w, "RIFF", 4);
    put_le32(w + 4, 36 + data_len);
    memcpy(w + 8, "WAVEfmt ", 8);
    put_le32(w + 16, 16);          /* PCM fmt chunk size */
    put_le16(w + 20, 1);           /* PCM */
    put_le16(w + 22, 1);           /* mono */
    put_le32(w + 24, RATE);
    put_le32(w + 28, RATE * 2);    /* byte rate */
    put_le16(w + 32, 2);           /* block align */
    put_le16(w + 34, 16);          /* bits */
    memcpy(w + 36, "data", 4);
    put_le32(w + 40, data_len);
    memcpy(w + 44, s_turn, data_len);
    *out_wav = w;
    *out_len = 44 + data_len;
    return ESP_OK;
}

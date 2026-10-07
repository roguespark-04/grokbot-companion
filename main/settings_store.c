/**
 * NVS settings store — see settings_store.h for the key map.
 */
#include "settings_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static const char *NS = "companion";

static device_settings_t s_cfg;
static SemaphoreHandle_t s_lock;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static uint32_t fnv1a32(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

/** NVS keys are max 15 chars. "voice_dr_eggbot" fits exactly; longer ids hash. */
static void voice_key(const char *bot_id, char key[16])
{
    if (strlen(bot_id) + 6 <= 15) {
        snprintf(key, 16, "voice_%s", bot_id);
    } else {
        snprintf(key, 16, "v_%08lx", (unsigned long)fnv1a32(bot_id));
    }
}

esp_err_t settings_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.talk_mode = TALK_MODE_PTT;            /* DEFAULT: press-to-talk */
    s_cfg.volume = SETTINGS_DEFAULT_VOLUME;
    s_cfg.brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    s_cfg.sleep_s = SETTINGS_DEFAULT_SLEEP_S;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_cfg.last_bot_id);
        if (nvs_get_str(h, "last_bot", s_cfg.last_bot_id, &len) != ESP_OK) {
            s_cfg.last_bot_id[0] = '\0';
        }
        uint8_t u8;
        uint16_t u16;
        if (nvs_get_u8(h, "talk_mode", &u8) == ESP_OK && u8 <= TALK_MODE_ALWAYS) {
            s_cfg.talk_mode = (talk_mode_t)u8;
        }
        if (nvs_get_u8(h, "volume", &u8) == ESP_OK && u8 <= 100) {
            s_cfg.volume = u8;
        }
        if (nvs_get_u8(h, "bright", &u8) == ESP_OK && u8 >= 5 && u8 <= 100) {
            s_cfg.brightness = u8;
        }
        if (nvs_get_u16(h, "sleep_s", &u16) == ESP_OK) {
            s_cfg.sleep_s = u16;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "loaded: last_bot=%s talk_mode=%s vol=%u bright=%u sleep=%us",
             s_cfg.last_bot_id[0] ? s_cfg.last_bot_id : "(none)",
             s_cfg.talk_mode == TALK_MODE_PTT ? "ptt" : "always",
             s_cfg.volume, s_cfg.brightness, s_cfg.sleep_s);
    return ESP_OK;
}

void settings_get(device_settings_t *out)
{
    lock();
    *out = s_cfg;
    unlock();
}

static esp_err_t commit_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t commit_str(const char *key, const char *v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_set_last_bot(const char *bot_id)
{
    if (!bot_id || !bot_id[0]) return ESP_ERR_INVALID_ARG;
    lock();
    bool same = strcmp(s_cfg.last_bot_id, bot_id) == 0;
    if (!same) {
        strlcpy(s_cfg.last_bot_id, bot_id, sizeof(s_cfg.last_bot_id));
    }
    unlock();
    return same ? ESP_OK : commit_str("last_bot", bot_id);
}

esp_err_t settings_set_talk_mode(talk_mode_t mode)
{
    lock(); s_cfg.talk_mode = mode; unlock();
    return commit_u8("talk_mode", (uint8_t)mode);
}

esp_err_t settings_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    lock(); s_cfg.volume = volume; unlock();
    return commit_u8("volume", volume);
}

esp_err_t settings_set_brightness(uint8_t pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    lock(); s_cfg.brightness = pct; unlock();
    return commit_u8("bright", pct);
}

esp_err_t settings_set_sleep_s(uint16_t seconds)
{
    lock(); s_cfg.sleep_s = seconds; unlock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, "sleep_s", seconds);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void settings_get_voice(const char *bot_id, const char *fallback, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    strlcpy(out, fallback ? fallback : "", out_len);
    if (!bot_id || !bot_id[0]) return;
    char key[16];
    voice_key(bot_id, key);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = out_len;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        strlcpy(out, fallback ? fallback : "", out_len);
    }
    nvs_close(h);
}

esp_err_t settings_set_voice(const char *bot_id, const char *voice_id)
{
    if (!bot_id || !bot_id[0] || !voice_id) return ESP_ERR_INVALID_ARG;
    char key[16];
    voice_key(bot_id, key);
    ESP_LOGI(TAG, "voice[%s] (%s) = %s", bot_id, key, voice_id);
    return commit_str(key, voice_id);
}

esp_err_t settings_blob_get(const char *key, char **out, size_t *out_len)
{
    *out = NULL;
    if (out_len) *out_len = 0;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = 0;
    err = nvs_get_blob(h, key, NULL, &len);
    if (err == ESP_OK && len > 0) {
        char *buf = malloc(len + 1);
        if (!buf) {
            err = ESP_ERR_NO_MEM;
        } else if ((err = nvs_get_blob(h, key, buf, &len)) == ESP_OK) {
            buf[len] = '\0';
            *out = buf;
            if (out_len) *out_len = len;
        } else {
            free(buf);
        }
    } else if (err == ESP_OK) {
        err = ESP_ERR_NOT_FOUND;
    }
    nvs_close(h);
    return err;
}

esp_err_t settings_blob_set(const char *key, const char *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

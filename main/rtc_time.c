/**
 * PCF85063 + SNTP.
 *
 * PCF85063 time registers 0x04..0x0A (BCD): seconds (bit7 = OS, oscillator stopped →
 * time invalid), minutes, hours (24h), days, weekdays, months, years (00..99 → 2000+).
 * The RTC keeps UTC; TZ is applied by newlib via setenv("TZ").
 *
 * HARDWARE TODO: confirm register layout / OS flag on the actual board RTC.
 */
#include "rtc_time.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "board_pins.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"

static const char *TAG = "rtc_time";
static i2c_master_dev_handle_t s_rtc;
static volatile time_source_t s_src = TIME_SRC_NONE;
static bool s_sntp_started;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t rtc_read(struct tm *out, bool *valid)
{
    uint8_t reg = 0x04, b[7];
    esp_err_t err = i2c_master_transmit_receive(s_rtc, &reg, 1, b, sizeof(b), 50);
    if (err != ESP_OK) return err;
    *valid = !(b[0] & 0x80);
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2bin(b[0] & 0x7F);
    out->tm_min = bcd2bin(b[1] & 0x7F);
    out->tm_hour = bcd2bin(b[2] & 0x3F);
    out->tm_mday = bcd2bin(b[3] & 0x3F);
    out->tm_wday = b[4] & 0x07;
    out->tm_mon = bcd2bin(b[5] & 0x1F) - 1;
    out->tm_year = bcd2bin(b[6]) + 100;
    return ESP_OK;
}

static esp_err_t rtc_write(const struct tm *t)
{
    uint8_t b[8] = {
        0x04,
        bin2bcd((uint8_t)t->tm_sec),   /* writing seconds clears OS flag */
        bin2bcd((uint8_t)t->tm_min),
        bin2bcd((uint8_t)t->tm_hour),
        bin2bcd((uint8_t)t->tm_mday),
        (uint8_t)(t->tm_wday & 0x07),
        bin2bcd((uint8_t)(t->tm_mon + 1)),
        bin2bcd((uint8_t)(t->tm_year % 100)),
    };
    return i2c_master_transmit(s_rtc, b, sizeof(b), 50);
}

static void on_sntp_sync(struct timeval *tv)
{
    s_src = TIME_SRC_SNTP;
    struct tm utc;
    gmtime_r(&tv->tv_sec, &utc);
    esp_err_t err = s_rtc ? rtc_write(&utc) : ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "SNTP synced; RTC write-back %s", esp_err_to_name(err));
}

esp_err_t rtc_time_init(void)
{
    setenv("TZ", CONFIG_MUSE_TZ, 1);
    tzset();
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MUSE_I2C_ADDR_PCF85063,
        .scl_speed_hz = MUSE_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_rtc);
    if (err != ESP_OK) return err;

    struct tm t;
    bool valid = false;
    if (rtc_read(&t, &valid) == ESP_OK && valid && t.tm_year >= 124) {
        /* RTC holds UTC: convert without TZ via timegm-equivalent. */
        char *old_tz = getenv("TZ") ? strdup(getenv("TZ")) : NULL;
        setenv("TZ", "UTC0", 1);
        tzset();
        time_t epoch = mktime(&t);
        if (old_tz) {
            setenv("TZ", old_tz, 1);
            free(old_tz);
        }
        tzset();
        struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
        settimeofday(&tv, NULL);
        s_src = TIME_SRC_RTC;
        ESP_LOGI(TAG, "clock restored from PCF85063");
    } else {
        ESP_LOGW(TAG, "RTC invalid / absent — waiting for SNTP");
    }
    return ESP_OK;
}

esp_err_t rtc_time_start_sntp(void)
{
    if (s_sntp_started) return ESP_OK;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MUSE_SNTP_SERVER);
    cfg.sync_cb = on_sntp_sync;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) s_sntp_started = true;
    return err;
}

time_source_t rtc_time_source(void) { return s_src; }
bool rtc_time_valid(void) { return s_src != TIME_SRC_NONE; }

const char *rtc_time_source_str(void)
{
    switch (s_src) {
    case TIME_SRC_RTC:  return "RTC";
    case TIME_SRC_SNTP: return "SNTP synced";
    default:            return "not set";
    }
}

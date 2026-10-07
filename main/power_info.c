/**
 * AXP2101 battery readout.
 *
 * Register map follows XPowersLib (used by Waveshare examples/esp-idf/01_AXP2101):
 *   0x00 STATUS1  bit5 VBUS good, bit3 battery present
 *   0x01 STATUS2  bits6:5 charge direction: 01 = charging, 10 = discharging, 00 = standby
 *   0x18 CHARGE_GAUGE_WDT_CTRL  bit3 fuel-gauge enable
 *   0x30 ADC_CHANNEL_CTRL       bit0 VBAT measurement enable
 *   0x34/0x35 VBAT ADC (14-bit, mV)
 *   0xA4 battery percent (fuel gauge)
 *
 * HARDWARE TODO: verify against XPowersLib on the real board (bit positions, and that the
 * BSP doesn't already own the PMIC device handle).
 */
#include "power_info.h"

#include <string.h>
#include "board_pins.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "power_info";
static i2c_master_dev_handle_t s_dev;

#define AXP_STATUS1      0x00
#define AXP_STATUS2      0x01
#define AXP_GAUGE_CTRL   0x18
#define AXP_ADC_CTRL     0x30
#define AXP_VBAT_H       0x34
#define AXP_VBAT_L       0x35
#define AXP_BAT_PERCENT  0xA4

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

static esp_err_t set_bit(uint8_t reg, uint8_t bit)
{
    uint8_t v = 0;
    esp_err_t err = rd(reg, &v);
    if (err != ESP_OK) return err;
    return wr(reg, v | (uint8_t)(1u << bit));
}

esp_err_t power_info_init(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MUSE_I2C_ADDR_AXP2101,
        .scl_speed_hz = MUSE_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_dev);
    if (err != ESP_OK) return err;
    /* Best effort: gauge + VBAT ADC on. Failures are logged, not fatal (board may be absent). */
    if (set_bit(AXP_GAUGE_CTRL, 3) != ESP_OK || set_bit(AXP_ADC_CTRL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not responding at 0x%02x (board attached?)", MUSE_I2C_ADDR_AXP2101);
    }
    return ESP_OK;
}

esp_err_t power_info_read(power_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->percent = -1;
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t s1, s2, pct, vh, vl;
    esp_err_t err = rd(AXP_STATUS1, &s1);
    if (err == ESP_OK) err = rd(AXP_STATUS2, &s2);
    if (err != ESP_OK) return err;
    out->vbus_present = (s1 >> 5) & 1;
    out->battery_present = (s1 >> 3) & 1;
    out->charging = ((s2 >> 5) & 0x03) == 0x01;
    if (out->battery_present) {
        if (rd(AXP_BAT_PERCENT, &pct) == ESP_OK && pct <= 100) out->percent = pct;
        if (rd(AXP_VBAT_H, &vh) == ESP_OK && rd(AXP_VBAT_L, &vl) == ESP_OK) {
            out->battery_mv = (uint16_t)(((vh & 0x3F) << 8) | vl);
        }
    }
    out->valid = true;
    return ESP_OK;
}

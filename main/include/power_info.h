/**
 * @file power_info.h
 * @brief Battery / charger readout from the AXP2101 PMIC (I2C 0x34).
 *
 * PWR button power on/off is handled by the AXP2101 itself (long-press PWRON);
 * firmware only reads status here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;          /* false until the first successful read */
    bool     battery_present;
    int      percent;        /* 0..100, -1 unknown */
    bool     charging;
    bool     vbus_present;   /* USB-C power */
    uint16_t battery_mv;     /* 0 if unknown */
} power_status_t;

/** Needs the shared I2C bus (bsp_i2c_init()). Enables fuel gauge + VBAT ADC. */
esp_err_t power_info_init(void);
esp_err_t power_info_read(power_status_t *out);

#ifdef __cplusplus
}
#endif

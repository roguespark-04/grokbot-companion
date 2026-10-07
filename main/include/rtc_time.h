/**
 * @file rtc_time.h
 * @brief Wall clock: PCF85063 RTC (I2C 0x51) at boot, SNTP when Wi-Fi is up, RTC write-back.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_SRC_NONE = 0,   /* clock not set */
    TIME_SRC_RTC,        /* restored from PCF85063 */
    TIME_SRC_SNTP,       /* synced from network this boot */
} time_source_t;

/** Sets TZ (Kconfig MUSE_TZ), reads the RTC and seeds system time if valid. */
esp_err_t     rtc_time_init(void);
/** Call once Wi-Fi has an IP; starts SNTP and writes the RTC after sync. */
esp_err_t     rtc_time_start_sntp(void);
time_source_t rtc_time_source(void);
bool          rtc_time_valid(void);
const char   *rtc_time_source_str(void);

#ifdef __cplusplus
}
#endif

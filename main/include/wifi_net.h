#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,      /* retrying with backoff */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char         ssid[33];
    int8_t       rssi;      /* dBm, 0 if unknown */
    char         ip[16];
} wifi_info_t;

/** Requires settings_init() (NVS) first. */
esp_err_t wifi_net_init(void);
esp_err_t wifi_net_connect(void);
bool      wifi_net_is_connected(void);
void      wifi_net_get_info(wifi_info_t *out);
/** Modem power save: true while locked (WIFI_PS_MAX_MODEM), false when awake (MIN_MODEM). */
void      wifi_net_set_low_power(bool low);
const char *wifi_state_str(wifi_state_t s);

#ifdef __cplusplus
}
#endif

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_net_init(void);
esp_err_t wifi_net_connect(void);
bool wifi_net_is_connected(void);

#ifdef __cplusplus
}
#endif

/**
 * Station Wi-Fi with reconnect + status for the device settings panel.
 * Credentials come from Kconfig placeholders (TODO: provisioning / NVS creds).
 */
#include "wifi_net.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_net";
static volatile wifi_state_t s_state = WIFI_STATE_OFF;
static char s_ip[16];
static int s_retry;
static TimerHandle_t s_retry_timer;

static void retry_cb(TimerHandle_t t)
{
    (void)t;
    s_state = WIFI_STATE_CONNECTING;
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = '\0';
        s_state = WIFI_STATE_FAILED;
        /* backoff 1s,2s,4s..30s */
        uint32_t delay_ms = 1000u << (s_retry < 5 ? s_retry : 5);
        if (delay_ms > 30000) delay_ms = 30000;
        s_retry++;
        ESP_LOGW(TAG, "disconnected; retry in %lu ms", (unsigned long)delay_ms);
        xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(delay_ms), 0);
        xTimerStart(s_retry_timer, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&ev->ip_info.ip, s_ip, sizeof(s_ip));
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "connected, ip=%s", s_ip);
    }
}

esp_err_t wifi_net_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(1000), pdFALSE, NULL, retry_cb);
    ESP_LOGI(TAG, "wifi init ok (SSID=%s)", CONFIG_MUSE_WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_net_connect(void)
{
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, CONFIG_MUSE_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_MUSE_WIFI_PASSWORD, sizeof(wc.sta.password));
    /* Needed for modem sleep to keep the association through light sleep. */
    wc.sta.listen_interval = 3;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());   /* STA_START → esp_wifi_connect() */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return ESP_OK;
}

bool wifi_net_is_connected(void) { return s_state == WIFI_STATE_CONNECTED; }

void wifi_net_get_info(wifi_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = s_state;
    strlcpy(out->ssid, CONFIG_MUSE_WIFI_SSID, sizeof(out->ssid));
    strlcpy(out->ip, s_ip, sizeof(out->ip));
    if (s_state == WIFI_STATE_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
            strlcpy(out->ssid, (const char *)ap.ssid, sizeof(out->ssid));
        }
    }
}

void wifi_net_set_low_power(bool low)
{
    esp_wifi_set_ps(low ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

const char *wifi_state_str(wifi_state_t s)
{
    switch (s) {
    case WIFI_STATE_CONNECTING: return "Connecting";
    case WIFI_STATE_CONNECTED:  return "Connected";
    case WIFI_STATE_FAILED:     return "Disconnected";
    default:                    return "Off";
    }
}

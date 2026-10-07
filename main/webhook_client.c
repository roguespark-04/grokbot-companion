/**
 * Device ↔ audio relay over HTTP(S). Relay URL + bearer come from Kconfig
 * (TODO: move token to NVS provisioning).
 */
#include "webhook_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "relay_client";

#define HTTP_TIMEOUT_MS   8000
#define JSON_MAX_BYTES    (32 * 1024)

typedef struct {
    const char *method_name;
    esp_http_client_method_t method;
    const char *url;
    const char *content_type;
    const uint8_t *body;
    size_t body_len;
    const char *hdr_names[4];
    const char *hdr_values[4];
} http_req_t;

/* ---------------------------------------------------------------- relay URLs */

static size_t base_len(void)
{
    size_t n = strlen(CONFIG_MUSE_UPLOAD_URL);
    while (n && CONFIG_MUSE_UPLOAD_URL[n - 1] == '/') n--;
    return n;
}

/** True when url is on the configured relay (only those requests carry the token). */
static bool is_relay_url(const char *url)
{
    size_t n = base_len();
    return url && strncmp(url, CONFIG_MUSE_UPLOAD_URL, n) == 0 &&
           (url[n] == '/' || url[n] == '\0' || url[n] == '?');
}

/**
 * The relay builds URLs from its own PUBLIC_BASE_URL (the tailnet address Meridian uses),
 * which the device can't reach through Funnel. Re-base relay-owned paths onto
 * CONFIG_MUSE_UPLOAD_URL; leave genuinely external URLs alone (they get no token).
 */
void webhook_client_relay_url(const char *in, char *out, size_t n)
{
    static const char *RELAY_PATHS[] = {"/replies/", "/result/", "/audio/", "/status/", "/voices/"};
    if (!in || !in[0]) {
        if (n) out[0] = '\0';
        return;
    }
    const char *path = NULL;
    if (in[0] == '/') {
        path = in;
    } else {
        const char *p = strstr(in, "://");
        if (p) {
            p = strchr(p + 3, '/');
            if (p) {
                for (size_t i = 0; i < sizeof(RELAY_PATHS) / sizeof(RELAY_PATHS[0]); i++) {
                    if (strncmp(p, RELAY_PATHS[i], strlen(RELAY_PATHS[i])) == 0) {
                        path = p;
                        break;
                    }
                }
            }
        }
    }
    if (path) {
        snprintf(out, n, "%.*s%s", (int)base_len(), CONFIG_MUSE_UPLOAD_URL, path);
    } else {
        strlcpy(out, in, n);
    }
}

static void add_auth(esp_http_client_handle_t c)
{
    char bearer[160];   /* esp_http_client_set_header copies the value */
    if (CONFIG_MUSE_DEVICE_RELAY_TOKEN[0]) {
        snprintf(bearer, sizeof(bearer), "Bearer %s", CONFIG_MUSE_DEVICE_RELAY_TOKEN);
        esp_http_client_set_header(c, "Authorization", bearer);
    }
    esp_http_client_set_header(c, "X-Device-Id", CONFIG_MUSE_DEVICE_ID);
}

/**
 * Generic request: writes body, reads full response into a PSRAM buffer (max_len).
 * Returns ESP_OK on transport success; check *status for HTTP code.
 */
static esp_err_t http_do(const http_req_t *r, uint8_t **resp, size_t *resp_len,
                         size_t max_len, int *status)
{
    *resp = NULL;
    if (resp_len) *resp_len = 0;
    *status = 0;
    esp_http_client_config_t cfg = {
        .url = r->url,
        .method = r->method,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* only used for https:// */
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    if (is_relay_url(r->url)) {
        add_auth(c);   /* never send the device token to a non-relay host */
    }
    if (r->content_type) esp_http_client_set_header(c, "Content-Type", r->content_type);
    for (int i = 0; i < 4 && r->hdr_names[i]; i++) {
        if (r->hdr_values[i] && r->hdr_values[i][0]) {
            esp_http_client_set_header(c, r->hdr_names[i], r->hdr_values[i]);
        }
    }

    esp_err_t err = esp_http_client_open(c, (int)r->body_len);
    if (err == ESP_OK && r->body_len) {
        size_t off = 0;
        while (off < r->body_len) {
            int w = esp_http_client_write(c, (const char *)r->body + off, (int)(r->body_len - off));
            if (w <= 0) { err = ESP_FAIL; break; }
            off += (size_t)w;
        }
    }
    if (err == ESP_OK) {
        int64_t clen = esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        size_t cap = (clen > 0 && (size_t)clen < max_len) ? (size_t)clen + 1 : 4096;
        uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = malloc(cap);
        size_t len = 0;
        while (buf) {
            if (len + 1 >= cap) {
                if (cap >= max_len) { err = ESP_ERR_INVALID_SIZE; break; }
                size_t ncap = cap * 2 > max_len ? max_len : cap * 2;
                uint8_t *nb = realloc(buf, ncap);
                if (!nb) { err = ESP_ERR_NO_MEM; break; }
                buf = nb;
                cap = ncap;
            }
            int n = esp_http_client_read(c, (char *)buf + len, (int)(cap - len - 1));
            if (n < 0) { err = ESP_FAIL; break; }
            if (n == 0) break;
            len += (size_t)n;
        }
        if (!buf) err = ESP_ERR_NO_MEM;
        if (err == ESP_OK) {
            buf[len] = '\0';
            *resp = buf;
            if (resp_len) *resp_len = len;
        } else {
            free(buf);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s %s failed: %s", r->method_name, r->url, esp_err_to_name(err));
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

static void copy_json_str(const cJSON *root, const char *key, char *dst, size_t n)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(it) && it->valuestring) strlcpy(dst, it->valuestring, n);
}

esp_err_t webhook_client_init(void)
{
    ESP_LOGI(TAG, "relay=%s device=%s auth=%s", CONFIG_MUSE_UPLOAD_URL, CONFIG_MUSE_DEVICE_ID,
             CONFIG_MUSE_DEVICE_RELAY_TOKEN[0] ? "bearer" : "NONE (set MUSE_DEVICE_RELAY_TOKEN)");
    return ESP_OK;
}

esp_err_t webhook_client_upload_audio(const uint8_t *wav, size_t wav_len,
                                      const relay_route_t *route, webhook_result_t *out)
{
    if (!out || !wav || !wav_len || !route || !route->target_bot_id) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    char url[192];
    snprintf(url, sizeof(url), "%.*s/upload", (int)base_len(), CONFIG_MUSE_UPLOAD_URL);
    http_req_t r = {
        .method_name = "POST", .method = HTTP_METHOD_POST, .url = url,
        .content_type = "audio/wav", .body = wav, .body_len = wav_len,
        .hdr_names = {"X-Target-Bot-Id", "X-Voice-Id", "X-Conversation-Id", NULL},
        .hdr_values = {route->target_bot_id, route->voice_id, route->conversation_id, NULL},
    };
    uint8_t *resp = NULL;
    int status = 0;
    esp_err_t err = http_do(&r, &resp, NULL, JSON_MAX_BYTES, &status);
    out->http_status = status;
    if (err != ESP_OK) return err;
    cJSON *root = cJSON_Parse((const char *)resp);
    if (status != 201 || !root) {
        if (root) copy_json_str(root, "error", out->error, sizeof(out->error));
        ESP_LOGW(TAG, "upload HTTP %d %s", status, out->error);
        cJSON_Delete(root);
        free(resp);
        return ESP_FAIL;
    }
    copy_json_str(root, "job_id", out->job_id, sizeof(out->job_id));
    copy_json_str(root, "audio_url", out->audio_url, sizeof(out->audio_url));
    copy_json_str(root, "result_url", out->result_url, sizeof(out->result_url));
    cJSON_Delete(root);
    free(resp);
    ESP_LOGI(TAG, "uploaded %u B → job %s (bot=%s voice=%s)", (unsigned)wav_len, out->job_id,
             route->target_bot_id, route->voice_id ? route->voice_id : "-");
    return out->job_id[0] ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t webhook_client_poll_result(const webhook_result_t *upload, webhook_result_t *out,
                                     relay_result_state_t *state)
{
    if (!upload || !out || !state) return ESP_ERR_INVALID_ARG;
    *state = RELAY_RESULT_PENDING;
    char url[256];
    snprintf(url, sizeof(url), "%.*s/result/%s", (int)base_len(), CONFIG_MUSE_UPLOAD_URL,
             upload->job_id);
    http_req_t r = {.method_name = "GET", .method = HTTP_METHOD_GET, .url = url};
    uint8_t *resp = NULL;
    int status = 0;
    esp_err_t err = http_do(&r, &resp, NULL, JSON_MAX_BYTES, &status);
    out->http_status = status;
    if (err != ESP_OK) return err;
    if (status == 202) {
        free(resp);
        return ESP_OK;
    }
    if (status == 410 || status == 404) {   /* expired / cleaned up on the relay */
        free(resp);
        *state = RELAY_RESULT_ERROR;
        strlcpy(out->error, status == 410 ? "Reply expired" : "Turn not found", sizeof(out->error));
        return ESP_OK;
    }
    cJSON *root = cJSON_Parse((const char *)resp);
    const cJSON *st = root ? cJSON_GetObjectItemCaseSensitive(root, "status") : NULL;
    if (status == 200 && cJSON_IsString(st) && strcmp(st->valuestring, "ready") == 0) {
        *state = RELAY_RESULT_READY;
        char raw_url[sizeof(out->reply_audio_url)] = "";
        copy_json_str(root, "reply_audio_url", raw_url, sizeof(raw_url));
        webhook_client_relay_url(raw_url, out->reply_audio_url, sizeof(out->reply_audio_url));
    } else if (cJSON_IsString(st) && strcmp(st->valuestring, "pending") == 0) {
        *state = RELAY_RESULT_PENDING;
    } else {
        *state = RELAY_RESULT_ERROR;
        if (root) copy_json_str(root, "message", out->error, sizeof(out->error));
    }
    cJSON_Delete(root);
    free(resp);
    return ESP_OK;
}

esp_err_t webhook_client_get_status(const char *bot_id, relay_status_t *out)
{
    if (!bot_id || !out) return ESP_ERR_INVALID_ARG;
    char path[64];
    snprintf(path, sizeof(path), "status/%s", bot_id);
    char *body = NULL;
    esp_err_t err = webhook_client_get_json(path, &body, NULL);
    if (err != ESP_OK) return err;
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    memset(out, 0, sizeof(*out));
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "state");
    out->state = muse_status_from_str(cJSON_IsString(st) ? st->valuestring : "idle");
    copy_json_str(root, "text", out->text, sizeof(out->text));
    out->stale = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "stale"));
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "updated_at_ms");
    out->updated_at_ms = cJSON_IsNumber(ts) ? (int64_t)ts->valuedouble : 0;
    if (out->stale) out->state = MUSE_STATUS_IDLE;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t webhook_client_get_json(const char *path, char **out, size_t *out_len)
{
    char url[192];
    snprintf(url, sizeof(url), "%.*s/%s", (int)base_len(), CONFIG_MUSE_UPLOAD_URL, path);
    http_req_t r = {.method_name = "GET", .method = HTTP_METHOD_GET, .url = url};
    uint8_t *resp = NULL;
    int status = 0;
    esp_err_t err = http_do(&r, &resp, out_len, JSON_MAX_BYTES, &status);
    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGW(TAG, "GET %s → HTTP %d", path, status);
        free(resp);
        return ESP_FAIL;
    }
    *out = (char *)resp;
    return ESP_OK;
}

esp_err_t webhook_client_download(const char *url_in, uint8_t **out, size_t *out_len, size_t max_len)
{
    char url[256];
    webhook_client_relay_url(url_in, url, sizeof(url));   /* /replies/ needs the device token */
    http_req_t r = {.method_name = "GET", .method = HTTP_METHOD_GET, .url = url};
    int status = 0;
    esp_err_t err = http_do(&r, out, out_len, max_len, &status);
    if (err == ESP_OK && status != 200) {
        ESP_LOGW(TAG, "download HTTP %d", status);
        free(*out);
        *out = NULL;
        return ESP_FAIL;
    }
    return err;
}

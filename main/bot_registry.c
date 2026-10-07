/**
 * Bot roster + voice registry.
 *
 * GET /bots   → {"default_bot_id": "...", "bots": [{id,name,shape,color,accent,default_voice_id?}]}
 * GET /voices → {"placeholder": true, "voices": [{id,name,description}]}
 */
#include "bot_registry.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "settings_store.h"

static const char *TAG = "bot_registry";

static bot_info_t   s_bots[BOT_MAX];
static int          s_bot_count;
static char         s_default_id[BOT_ID_MAX] = "meridian";
static voice_info_t s_voices[VOICE_MAX];
static int          s_voice_count;
static bool         s_voices_placeholder = true;

/* Compiled-in roster so the carousel works on first boot with no network.
 * Mirrors relay/bots.json — the relay copy wins once fetched. */
static const char *BUILTIN_BOTS_JSON =
    "{\"default_bot_id\":\"meridian\",\"bots\":["
    "{\"id\":\"meridian\",\"name\":\"Meridian\",\"shape\":\"circle\",\"color\":\"#3B82F6\",\"accent\":\"#BFDBFE\",\"default_voice_id\":\"placeholder-calm\"},"
    "{\"id\":\"spark\",\"name\":\"Spark\",\"shape\":\"star\",\"color\":\"#F59E0B\",\"accent\":\"#FEF3C7\",\"default_voice_id\":\"placeholder-bright\"},"
    "{\"id\":\"quark\",\"name\":\"Quark\",\"shape\":\"hexagon\",\"color\":\"#8B5CF6\",\"accent\":\"#DDD6FE\",\"default_voice_id\":\"placeholder-crisp\"},"
    "{\"id\":\"scribe\",\"name\":\"Scribe\",\"shape\":\"squircle\",\"color\":\"#14B8A6\",\"accent\":\"#CCFBF1\",\"default_voice_id\":\"placeholder-warm\"},"
    "{\"id\":\"photon\",\"name\":\"Photon\",\"shape\":\"diamond\",\"color\":\"#FACC15\",\"accent\":\"#FEF9C3\",\"default_voice_id\":\"placeholder-bright\"},"
    "{\"id\":\"dr_eggbot\",\"name\":\"dr eggbot\",\"shape\":\"blob\",\"color\":\"#F5E6C8\",\"accent\":\"#FB923C\",\"default_voice_id\":\"placeholder-playful\"},"
    "{\"id\":\"pulse\",\"name\":\"Pulse\",\"shape\":\"ring\",\"color\":\"#EF4444\",\"accent\":\"#FECACA\",\"default_voice_id\":\"placeholder-crisp\"},"
    "{\"id\":\"proton\",\"name\":\"Proton\",\"shape\":\"octagon\",\"color\":\"#22C55E\",\"accent\":\"#BBF7D0\",\"default_voice_id\":\"placeholder-deep\"},"
    "{\"id\":\"clay\",\"name\":\"Clay\",\"shape\":\"pill\",\"color\":\"#C2410C\",\"accent\":\"#FED7AA\",\"default_voice_id\":\"placeholder-warm\"},"
    "{\"id\":\"nexus\",\"name\":\"Nexus\",\"shape\":\"triangle\",\"color\":\"#EC4899\",\"accent\":\"#FBCFE8\",\"default_voice_id\":\"placeholder-calm\"}"
    "]}";

static const char *BUILTIN_VOICES_JSON =
    "{\"placeholder\":true,\"voices\":["
    "{\"id\":\"placeholder-calm\",\"name\":\"Calm (placeholder)\",\"description\":\"Placeholder: even, unhurried delivery.\"},"
    "{\"id\":\"placeholder-warm\",\"name\":\"Warm (placeholder)\",\"description\":\"Placeholder: friendly, softer tone.\"},"
    "{\"id\":\"placeholder-bright\",\"name\":\"Bright (placeholder)\",\"description\":\"Placeholder: upbeat, energetic.\"},"
    "{\"id\":\"placeholder-crisp\",\"name\":\"Crisp (placeholder)\",\"description\":\"Placeholder: clear and concise.\"},"
    "{\"id\":\"placeholder-deep\",\"name\":\"Deep (placeholder)\",\"description\":\"Placeholder: lower pitch.\"},"
    "{\"id\":\"placeholder-playful\",\"name\":\"Playful (placeholder)\",\"description\":\"Placeholder: lighter, more animated.\"}"
    "]}";

/* ---------------------------------------------------------------- helpers */

static const char *SHAPE_NAMES[BOT_SHAPE_COUNT] = {
    "circle", "squircle", "hexagon", "diamond", "triangle",
    "star", "ring", "pill", "octagon", "blob",
};

bot_shape_t bot_shape_from_str(const char *s)
{
    if (s) {
        for (int i = 0; i < BOT_SHAPE_COUNT; i++) {
            if (strcmp(s, SHAPE_NAMES[i]) == 0) return (bot_shape_t)i;
        }
    }
    return BOT_SHAPE_CIRCLE;
}

const char *bot_shape_str(bot_shape_t s)
{
    return (s >= 0 && s < BOT_SHAPE_COUNT) ? SHAPE_NAMES[s] : "circle";
}

uint32_t bot_color_from_hex(const char *hex, uint32_t fallback)
{
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return fallback;
    char *end = NULL;
    unsigned long v = strtoul(hex + 1, &end, 16);
    return (end && *end == '\0') ? (uint32_t)v : fallback;
}

static const char *jstr(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

/* ---------------------------------------------------------------- bots */

static esp_err_t parse_bots(const char *json, size_t len, bot_info_t *out, int *count,
                            char default_id[BOT_ID_MAX])
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "bots");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    int n = 0;
    const cJSON *b;
    cJSON_ArrayForEach(b, arr) {
        if (n >= BOT_MAX) {
            ESP_LOGW(TAG, "roster truncated at %d bots", BOT_MAX);
            break;
        }
        const char *id = jstr(b, "id");
        if (!id || !id[0]) continue;
        bot_info_t *bi = &out[n];
        memset(bi, 0, sizeof(*bi));
        strlcpy(bi->id, id, sizeof(bi->id));
        strlcpy(bi->name, jstr(b, "name") ? jstr(b, "name") : id, sizeof(bi->name));
        bi->shape = bot_shape_from_str(jstr(b, "shape"));
        bi->color = bot_color_from_hex(jstr(b, "color"), 0x888888);
        bi->accent = bot_color_from_hex(jstr(b, "accent"), 0xFFFFFF);
        const char *dv = jstr(b, "default_voice_id");
        if (dv) strlcpy(bi->default_voice_id, dv, sizeof(bi->default_voice_id));
        n++;
    }
    const char *def = jstr(root, "default_bot_id");
    strlcpy(default_id, (def && def[0]) ? def : (n ? out[0].id : "meridian"), BOT_ID_MAX);
    cJSON_Delete(root);
    if (n == 0) return ESP_ERR_NOT_FOUND;
    *count = n;
    return ESP_OK;
}

esp_err_t bot_registry_update_from_json(const char *json, size_t len, bool persist, bool *changed)
{
    static bot_info_t tmp[BOT_MAX];   /* static: keep it off the task stack */
    int n = 0;
    char def[BOT_ID_MAX];
    esp_err_t err = parse_bots(json, len, tmp, &n, def);
    if (changed) *changed = false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bots JSON rejected: %s", esp_err_to_name(err));
        return err;
    }
    bool diff = (n != s_bot_count) || memcmp(tmp, s_bots, sizeof(bot_info_t) * n) != 0 ||
                strcmp(def, s_default_id) != 0;
    if (diff) {
        memcpy(s_bots, tmp, sizeof(bot_info_t) * n);
        s_bot_count = n;
        strlcpy(s_default_id, def, sizeof(s_default_id));
        if (persist) {
            esp_err_t e = settings_blob_set("bots_json", json, len);
            if (e != ESP_OK) ESP_LOGW(TAG, "bots cache write failed: %s", esp_err_to_name(e));
        }
        ESP_LOGI(TAG, "roster: %d bots (default %s)", n, s_default_id);
    }
    if (changed) *changed = diff;
    return ESP_OK;
}

/* ---------------------------------------------------------------- voices */

esp_err_t voice_registry_update_from_json(const char *json, size_t len, bool persist, bool *changed)
{
    static voice_info_t tmp[VOICE_MAX];
    if (changed) *changed = false;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "voices");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    int n = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr) {
        if (n >= VOICE_MAX) break;
        const char *id = jstr(v, "id");
        if (!id || !id[0]) continue;
        memset(&tmp[n], 0, sizeof(tmp[n]));
        strlcpy(tmp[n].id, id, sizeof(tmp[n].id));
        strlcpy(tmp[n].name, jstr(v, "name") ? jstr(v, "name") : id, sizeof(tmp[n].name));
        if (jstr(v, "description")) {
            strlcpy(tmp[n].description, jstr(v, "description"), sizeof(tmp[n].description));
        }
        n++;
    }
    const cJSON *ph = cJSON_GetObjectItemCaseSensitive(root, "placeholder");
    bool placeholder = cJSON_IsBool(ph) ? cJSON_IsTrue(ph) : false;
    cJSON_Delete(root);
    if (n == 0) return ESP_ERR_NOT_FOUND;

    bool diff = (n != s_voice_count) || placeholder != s_voices_placeholder ||
                memcmp(tmp, s_voices, sizeof(voice_info_t) * n) != 0;
    if (diff) {
        memcpy(s_voices, tmp, sizeof(voice_info_t) * n);
        s_voice_count = n;
        s_voices_placeholder = placeholder;
        if (persist) {
            esp_err_t e = settings_blob_set("voices_js", json, len);
            if (e != ESP_OK) ESP_LOGW(TAG, "voices cache write failed: %s", esp_err_to_name(e));
        }
        ESP_LOGI(TAG, "voices: %d%s", n, placeholder ? " (placeholder set)" : "");
    }
    if (changed) *changed = diff;
    return ESP_OK;
}

/* ---------------------------------------------------------------- init / getters */

void bot_registry_init(void)
{
    char *json = NULL;
    size_t len = 0;
    if (settings_blob_get("bots_json", &json, &len) == ESP_OK &&
        bot_registry_update_from_json(json, len, false, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "roster from NVS cache (%d bots)", s_bot_count);
    } else {
        ESP_ERROR_CHECK(bot_registry_update_from_json(BUILTIN_BOTS_JSON,
                                                      strlen(BUILTIN_BOTS_JSON), false, NULL));
        ESP_LOGI(TAG, "roster from built-in defaults (%d bots)", s_bot_count);
    }
    free(json);
    json = NULL;
    if (settings_blob_get("voices_js", &json, &len) != ESP_OK ||
        voice_registry_update_from_json(json, len, false, NULL) != ESP_OK) {
        ESP_ERROR_CHECK(voice_registry_update_from_json(BUILTIN_VOICES_JSON,
                                                        strlen(BUILTIN_VOICES_JSON), false, NULL));
    }
    free(json);
}

int bot_registry_count(void) { return s_bot_count; }
const bot_info_t *bot_registry_all(void) { return s_bots; }
const char *bot_registry_default_id(void) { return s_default_id; }

const bot_info_t *bot_registry_get(int idx)
{
    return (idx >= 0 && idx < s_bot_count) ? &s_bots[idx] : NULL;
}

int bot_registry_index_of(const char *bot_id)
{
    if (!bot_id) return -1;
    for (int i = 0; i < s_bot_count; i++) {
        if (strcmp(s_bots[i].id, bot_id) == 0) return i;
    }
    return -1;
}

int voice_registry_count(void) { return s_voice_count; }
const voice_info_t *voice_registry_all(void) { return s_voices; }
bool voice_registry_is_placeholder(void) { return s_voices_placeholder; }

const voice_info_t *voice_registry_get(int idx)
{
    return (idx >= 0 && idx < s_voice_count) ? &s_voices[idx] : NULL;
}

int voice_registry_index_of(const char *voice_id)
{
    if (!voice_id) return -1;
    for (int i = 0; i < s_voice_count; i++) {
        if (strcmp(s_voices[i].id, voice_id) == 0) return i;
    }
    return -1;
}

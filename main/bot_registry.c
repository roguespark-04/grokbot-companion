/**
 * Bot roster + voice registry.
 *
 * GET /bots   → {"default_bot_id": "...", "bots": [{id,name,shape,color,accent,rim?,shape_scale,
 *                shape_rotation,shape_wobble,shape_seed,avatar_tbd,default_voice_id?}]}
 * GET /voices → {"placeholder": false, "voices": [{id,name,description,sample_path?}]}
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
    "{\"id\":\"meridian\",\"name\":\"Meridian\",\"shape\":\"blob\",\"color\":\"#9CA3AF\",\"accent\":\"#E5E7EB\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":11,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"spark\",\"name\":\"Spark\",\"shape\":\"circle\",\"color\":\"#64748B\",\"accent\":\"#CBD5E1\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":33,\"avatar_tbd\":true,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"quark\",\"name\":\"Quark\",\"shape\":\"circle\",\"color\":\"#64748B\",\"accent\":\"#CBD5E1\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":36,\"avatar_tbd\":true,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"scribe\",\"name\":\"Scribe\",\"shape\":\"teardrop\",\"color\":\"#1E1F24\",\"accent\":\"#9CA3AF\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":120,\"avatar_tbd\":false,\"rim\":\"#5B616B\",\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"photon\",\"name\":\"Photon\",\"shape\":\"teardrop\",\"color\":\"#FACC15\",\"accent\":\"#FEF9C3\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":152,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"dr_eggbot\",\"name\":\"dr eggbot\",\"shape\":\"teardrop\",\"color\":\"#EF4444\",\"accent\":\"#FECACA\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":173,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"pulse\",\"name\":\"Pulse\",\"shape\":\"cloud\",\"color\":\"#3B82F6\",\"accent\":\"#BFDBFE\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":41,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"proton\",\"name\":\"Proton\",\"shape\":\"hex\",\"color\":\"#22C55E\",\"accent\":\"#BBF7D0\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":162,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    ","
    "{\"id\":\"clay\",\"name\":\"Clay\",\"shape\":\"squircle\",\"color\":\"#8B5A2B\",\"accent\":\"#E7C9A0\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":169,\"avatar_tbd\":false,\"default_voice_id\":\"cosmo\"}"
    ","
    "{\"id\":\"nexus\",\"name\":\"Nexus\",\"shape\":\"blob\",\"color\":\"#8B5CF6\",\"accent\":\"#DDD6FE\",\"shape_scale\":86,\"shape_rotation\":0,\"shape_wobble\":50,\"shape_seed\":97,\"avatar_tbd\":false,\"default_voice_id\":\"eve\"}"
    "]}";

/* The Grok Bot app voice list (xAI grok-tts ids). Mirrors relay/voices.json. */
static const char *BUILTIN_VOICES_JSON =
    "{\"placeholder\":false,\"voices\":["
    "{\"id\":\"altair\",\"name\":\"Altair\",\"description\":\"\"}"
    ","
    "{\"id\":\"ara\",\"name\":\"Ara\",\"description\":\"Warm and friendly\"}"
    ","
    "{\"id\":\"atlas\",\"name\":\"Atlas\",\"description\":\"Confident, commanding\"}"
    ","
    "{\"id\":\"aurora\",\"name\":\"Aurora\",\"description\":\"Serene, steady\"}"
    ","
    "{\"id\":\"carina\",\"name\":\"Carina\",\"description\":\"\"}"
    ","
    "{\"id\":\"castor\",\"name\":\"Castor\",\"description\":\"Charismatic, easygoing\"}"
    ","
    "{\"id\":\"celeste\",\"name\":\"Celeste\",\"description\":\"Compassionate, reassuring\"}"
    ","
    "{\"id\":\"cosmo\",\"name\":\"Cosmo\",\"description\":\"Bright, curious\"}"
    ","
    "{\"id\":\"eve\",\"name\":\"Eve\",\"description\":\"Energetic and upbeat (default)\"}"
    ","
    "{\"id\":\"helios\",\"name\":\"Helios\",\"description\":\"Upbeat, energetic\"}"
    ","
    "{\"id\":\"helix\",\"name\":\"Helix\",\"description\":\"\"}"
    ","
    "{\"id\":\"iris\",\"name\":\"Iris\",\"description\":\"\"}"
    ","
    "{\"id\":\"kepler\",\"name\":\"Kepler\",\"description\":\"Inventive, charismatic\"}"
    ","
    "{\"id\":\"leo\",\"name\":\"Leo\",\"description\":\"Authoritative and strong\"}"
    ","
    "{\"id\":\"liora\",\"name\":\"Liora\",\"description\":\"Calm, grounded\"}"
    ","
    "{\"id\":\"lumen\",\"name\":\"Lumen\",\"description\":\"Warm, articulate\"}"
    ","
    "{\"id\":\"luna\",\"name\":\"Luna\",\"description\":\"\"}"
    ","
    "{\"id\":\"lux\",\"name\":\"Lux\",\"description\":\"Grounded, calm\"}"
    ","
    "{\"id\":\"naksh\",\"name\":\"Naksh\",\"description\":\"Warm, thoughtful (Indian)\"}"
    ","
    "{\"id\":\"orion\",\"name\":\"Orion\",\"description\":\"\"}"
    ","
    "{\"id\":\"perseus\",\"name\":\"Perseus\",\"description\":\"Strong, confident\"}"
    ","
    "{\"id\":\"rex\",\"name\":\"Rex\",\"description\":\"Confident and clear\"}"
    ","
    "{\"id\":\"rigel\",\"name\":\"Rigel\",\"description\":\"Precise, calm (Australian)\"}"
    ","
    "{\"id\":\"sal\",\"name\":\"Sal\",\"description\":\"Smooth and balanced\"}"
    ","
    "{\"id\":\"sirius\",\"name\":\"Sirius\",\"description\":\"Quick-witted, playful\"}"
    ","
    "{\"id\":\"ursa\",\"name\":\"Ursa\",\"description\":\"Friendly, warm\"}"
    ","
    "{\"id\":\"zagan\",\"name\":\"Zagan\",\"description\":\"\"}"
    ","
    "{\"id\":\"zenith\",\"name\":\"Zenith\",\"description\":\"\"}"
    "]}";

/* ---------------------------------------------------------------- helpers */

static const char *SHAPE_NAMES[BOT_SHAPE_COUNT] = {
    "circle", "blob", "teardrop", "cloud", "hex", "squircle",
};

bot_shape_t bot_shape_from_str(const char *s)
{
    if (s) {
        for (int i = 0; i < BOT_SHAPE_COUNT; i++) {
            if (strcmp(s, SHAPE_NAMES[i]) == 0) return (bot_shape_t)i;
        }
        if (strcmp(s, "hexagon") == 0) return BOT_SHAPE_HEX;
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

static int jint(const cJSON *obj, const char *key, int lo, int hi, int def)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) return def;
    int v = (int)it->valuedouble;
    return v < lo ? lo : (v > hi ? hi : v);
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
        bi->has_rim = jstr(b, "rim") != NULL;
        bi->rim = bot_color_from_hex(jstr(b, "rim"), 0x5B616B);
        bi->scale_pct = (uint8_t)jint(b, "shape_scale", 50, 100, 86);
        bi->rotation = (int16_t)jint(b, "shape_rotation", -180, 180, 0);
        bi->wobble = (uint8_t)jint(b, "shape_wobble", 0, 100, 50);
        bi->seed = (uint8_t)jint(b, "shape_seed", 0, 255, 0);
        bi->tbd = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(b, "avatar_tbd"));
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
        tmp[n].has_sample = jstr(v, "sample_path") != NULL;
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

# Webhook / relay contract (v1 LOCKED, v1.1 multi-bot, v1.2 direct per-bot wakes + public HTTPS)

**Product:** Grok Bot Companion (repo `grokbot-companion`).
**Device role:** thin client. It talks **only** to the audio relay.
**Bots:** each bot can have its own **direct wake webhook**. A bot without one is reached
through **Meridian's companion wake** webhook, which routes by `target_bot_id`.

---

## Architecture (required)

```
ESP32 --HTTPS + device token--> https://grokbot-box.tail4de099.ts.net   (Tailscale Funnel)
                                   | proxies to 127.0.0.1:8787 on the box
                                   v
                              Audio relay (Flask)  -- JSON wake (Bearer bot key) -->  selected bot's webhook
                                   ^                                                  (or Meridian's, fallback)
                                   |  internal token, on the box (127.0.0.1 / tailnet)
                              Bots / Meridian: GET /audio, PUT /internal/status, PUT /internal/result
ESP32 <--poll /result, GET /replies-- relay
```

| Rule | Detail |
| --- | --- |
| No media on wakes | Wake body is **JSON only**. Never attach audio bytes to a bot webhook. |
| No keys on the ESP32 | The relay holds every webhook key (`relay/.env`) and POSTs the wake. |
| Device ↔ relay only | Public HTTPS via Funnel, `Authorization: Bearer <DEVICE_TOKEN>` on every call. TLS is verified with the ESP-IDF certificate bundle. |
| Internal side stays on the box | Bots and Meridian use `http://127.0.0.1:8787` (or the tailnet address in `PUBLIC_BASE_URL`) with `INTERNAL_TOKEN`. |
| Audio format | Upload: WAV/PCM **16 kHz mono**. Reply: PCM WAV (RIFF/WAVE), at most 4 MiB. |

---

## (a) Wake webhook (per bot, or Meridian as fallback)

**JSON-only.** No media bytes. Same body for a direct wake and a fallback wake.

### Fields

| Field | Type | Required | Notes |
| --- | --- | --- | --- |
| `action` | string | yes | `probe` or `voice_turn` |
| `job_id` | string | yes | Correlates upload ↔ result ↔ turn |
| `device_id` | string | yes | Stable device identity |
| `audio_url` | string | yes for `voice_turn` | Relay URL of the upload. **Needs `Authorization: Bearer <INTERNAL_TOKEN>`** (v1.2) |
| `timestamp_ms` | number | yes | Unix epoch milliseconds |
| `transcript` | string | no | Optional; may be ignored |
| `result_url` | string | v1.1 | Relay URL the device polls for this `job_id` |
| `target_bot_id` | string | v1.1 | Bot Frank picked on the carousel (`meridian`, `spark`, `quark`, `scribe`, `photon`, `dr_eggbot`, `pulse`, `proton`, `clay`, `nexus`; see `GET /bots`). On a fallback wake, **Meridian routes the turn to this bot** |
| `target_bot_name` | string | v1.1 | Display name for the same bot (e.g. `dr eggbot`) |
| `voice_id` | string \| null | v1.1 | The Grok Bot app voice id Frank chose for that bot (e.g. `eve`, `cosmo`). **The reply side synthesizes with xAI grok-tts using this id** (backend pending); the device only passes it. Falls back to the bot's `default_voice_id` |
| `conversation_id` | string \| null | v1.1 | Set while a conversation is open on the device (Start/End). Group turns with it. `null` = one-off turn. There are **no** separate conversation start/end wakes |
| `status_url` | string | v1.1 | `GET /status/<target_bot_id>` on the relay (what the device shows) |
| `wake_route` | string | v1.2 | `direct` (this is the bot's own webhook) or `meridian_fallback` |

### Actions

| `action` | Behavior |
| --- | --- |
| `probe` | **Ignore** (health check). No turn. Carries `target_bot_id` + `wake_route`. |
| `voice_turn` | Fetch `audio_url` (internal bearer), handle the ask as `target_bot_id`, **write the reply** with `PUT /internal/result/<job_id>`. |

### Example wake body (relay → Photon's own webhook, v1.2)

```json
{
  "action": "voice_turn",
  "job_id": "j_01HXYZ",
  "device_id": "pocket-001",
  "audio_url": "http://100.106.229.101:8787/audio/j_01HXYZ.wav",
  "result_url": "http://100.106.229.101:8787/result/j_01HXYZ",
  "timestamp_ms": 1728262800000,
  "target_bot_id": "photon",
  "target_bot_name": "Photon",
  "voice_id": "eve",
  "conversation_id": "c_9f2a11c0",
  "status_url": "http://100.106.229.101:8787/status/photon",
  "wake_route": "direct"
}
```

URLs in the wake use `PUBLIC_BASE_URL` (the tailnet address), because the bots run on the
same box. The device never uses those hosts: it re-bases relay paths onto its own HTTPS base.

### Routing (v1.2): direct per-bot wakes

* `relay/.env` may hold `WAKE_URL_<BOT>` + `WAKE_KEY_<BOT>` per bot (bot id upper-cased,
  non-alphanumerics → `_`: `WAKE_URL_PHOTON`, `WAKE_KEY_DR_EGGBOT`). Meridian's own entry
  stays `MERIDIAN_WEBHOOK_URL` / `MERIDIAN_SENDER_KEY`.
* A `voice_turn` for bot X goes to X's webhook if both are set (`wake_route=direct`),
  otherwise to Meridian's (`wake_route=meridian_fallback`). Same headers
  (`Authorization: Bearer <key>` + `X-Automation-Key`), 8 s timeout, one try. Failures are
  appended to `DATA_DIR/wake_fail.log` with `bot_id` and `route`.
* `wake_route` is stored in the job JSON and returned by `POST /upload`.
* Configure with `relay/set-bot-webhook.sh <bot_id> <url> <ENV_VAR_WITH_KEY>` (reads the key
  from that env var, never prints it). The relay re-reads `.env` on each wake: no restart.
* `GET /internal/wake-config` (internal bearer) lists each bot's route and webhook host,
  never keys or URL paths. `POST /probe?bot=<id>` (device or internal bearer) sends
  `action=probe` down that bot's route.

### HTTP response (wake-only)

The webhook's HTTP response is a **wake acknowledgement only** (typically 200), never reply
audio. Replies go through the relay (b).

---

## (b) Reply path

### Writing the reply (bot side, on the box)

`PUT /internal/result/<job_id>` with `Authorization: Bearer <INTERNAL_TOKEN>`:

| Body | Result |
| --- | --- |
| `Content-Type: audio/wav`, raw WAV | Validated: RIFF/WAVE, PCM (format 1 or extensible-PCM), 1-2 ch, 8-48 kHz. Bad header → `400`. Over 4 MiB → `413`. OK → job `ready` |
| JSON `{"status":"ready", ...}` | Job `ready` (extra keys like `text`, `transcript`, `reply_audio_url` are kept) |
| JSON `{"status":"error","message":"..."}` | Job `error` |
| JSON without `status` | `400`. **Only `status` decides**: a `message`/`error` key on a `ready` result is just detail |

### Device poll

`GET /result/<job_id>` (device bearer):

| Status | Meaning |
| --- | --- |
| `202` / `{ "status": "pending" }` | Still thinking. Poll again |
| `200` + `{ "status": "ready", "reply_audio_url": "..." }` | Download `GET /replies/<job_id>.wav` (device bearer) and play |
| `200` + `{ "status": "error", ... }` | Avatar → error, then idle |
| `410` | Job expired (`JOB_TTL_MS`, default 5 min; a reply extends it by `REPLY_TTL_MS`) |
| `404` | Unknown job, or cleaned up (expired jobs are deleted `JOB_RETAIN_MS`, default 1 h, later) |

---

## (b2) Multi-bot: roster, voices, live status

### Voice: `voice_id`

* `GET /voices` serves `relay/voices.json`: **the 28 Grok Bot app voices, in app order**
  (`altair`, `ara`, `atlas`, `aurora`, `carina`, `castor`, `celeste`, `cosmo`, `eve`, `helios`,
  `helix`, `iris`, `kepler`, `leo`, `liora`, `lumen`, `luna`, `lux`, `naksh`, `orion`,
  `perseus`, `rex`, `rigel`, `sal`, `sirius`, `ursa`, `zagan`, `zenith`), `placeholder:false`.
  These are xAI grok-tts voice ids. Descriptions come from xAI's docs where known.
* Each bot has its own voice on the device (NVS `voice_<bot_id>`, falling back to the bot's
  `default_voice_id`): Clay = `cosmo` (his app setting), every other bot = `eve` (xAI's default;
  they have no voice set in the app).
* The device sends it as `X-Voice-Id`; the relay stores it in the job and forwards it in the
  wake. **The reply side should synthesize with xAI grok-tts using that id** (backend
  pending). Unknown ids are forwarded unchanged; a `default_voice_id` missing from
  `voices.json` is logged as a warning, not dropped.
* Optional/TODO: `GET /voices/<id>/sample.wav` (device or internal bearer) serves preview clips
  from `VOICE_SAMPLES_DIR` (`DATA_DIR/voice_samples/<id>.wav`). `GET /voices` adds
  `sample_path` only for voices that have a clip, and the device enables Preview only then.
  No clips exist yet; they're to be generated with xAI TTS.

### Live status: the line under the avatar

| Method | Path | Auth | Body / response |
| --- | --- | --- | --- |
| `GET` | `/status/<bot_id>` | device or internal bearer | `{bot_id, state, text, updated_at_ms, stale, job_id?, source}` |
| `PUT` | `/internal/status/<bot_id>` | **internal bearer** | `{ "state": "...", "text": "...", "job_id": "j_..." }` |

* `state` ∈ `idle` · `listening` · `thinking` · `working` · `speaking` · `error`.
* `text` is **forced to ASCII and at most 120 chars by the relay** (curly quotes/dashes are
  mapped, accents stripped, emoji dropped, long text cut with `...`). The device fonts are ASCII.
* On upload the relay sets `thinking`: `"Sent to Photon"` on a direct wake, `"Meridian is
  passing this to Photon"` on a fallback. When the result lands it sets `idle` /
  `"Reply ready"` or `error`. Bots can override at any time; pushing `working` during long
  tasks keeps the device awake and the line live.
* A non-idle status older than `STATUS_STALE_MS` (10 min) is reported `stale: true` and the
  device shows idle.
* The device polls about every 2 s while a conversation is open and during a turn, never
  while locked.

### Roster: `GET /bots`

Served from `relay/bots.json` v2 (edit without restart). Each bot's look mirrors its app
profile (`avatarShape` / `avatarColor`); palette names are resolved to hex by the relay:

```json
{"version":1,"default_bot_id":"meridian","bots":[
  {"id":"photon","name":"Photon","shape":"teardrop","color":"#FACC15","accent":"#FEF9C3",
   "color_name":"yellow","shape_scale":86,"shape_rotation":0,"shape_wobble":50,"shape_seed":152,
   "avatar_tbd":false,"default_voice_id":"eve"}
]}
```

`shape` ∈ blob · teardrop · cloud · hex · squircle · circle (neutral default). Optional
`rim` (#RRGGBB) outlines dark bodies (Scribe is black). `avatar_tbd: true` marks bots whose
app look is still unknown (Spark, Quark). The device caches the last good body in NVS.

---

## (c) Audio relay endpoints (Spark, on the box)

| Method | Path | Auth |
| --- | --- | --- |
| `GET` | `/health` | none |
| `POST` | `/upload` | device |
| `GET` | `/result/<job_id>` | device |
| `GET` | `/replies/<job_id>.wav` | device or internal |
| `GET` | `/audio/<job_id>.wav` | **internal** |
| `GET` | `/bots`, `/voices`, `/status/<bot_id>`, `/voices/<id>/sample.wav` | device or internal |
| `POST` | `/probe[?bot=<id>]` | device or internal |
| `PUT` | `/internal/result/<job_id>`, `/internal/status/<bot_id>` | internal |
| `GET` | `/internal/wake-config` | internal |

Upload headers: `X-Device-Id`, `X-Target-Bot-Id`, `X-Voice-Id`, `X-Conversation-Id` (query
params accepted for curl). An unknown `target_bot_id` returns `400`, **including when
`bots.json` is missing or empty** (fails closed). The upload response adds `status_url`,
`target_bot_id`, `voice_id`, `wake_route`, `woken`.

See [`relay/README.md`](relay/README.md).

---

## Audio codec notes (device)

| Path | Chip | Notes |
| --- | --- | --- |
| Capture | **ES7210** | Dual mics + AEC reference (MIC3 ← ES8311 playback) |
| Playback | **ES8311** | I2S → speaker (NS4150B PA) |
| PTT | **BOOT** (GPIO0) | Hold-to-talk. PWR = power only |

Upload encoding: **WAV container, PCM s16le, 16 kHz, mono**.

---

## Kconfig mapping (firmware)

| Kconfig | Who uses it |
| --- | --- |
| `MUSE_UPLOAD_URL` | Device: relay base, default `https://grokbot-box.tail4de099.ts.net` (Funnel) |
| `MUSE_DEVICE_RELAY_TOKEN` | Device: bearer for every relay call (sent only to that host) |
| `MUSE_DEVICE_ID` | Device: sent on upload; relay echoes it into the wake |
| `MUSE_WEBHOOK_URL` / `MUSE_SENDER_BEARER_TOKEN` | **Relay only** (warning placeholders; never flashed) |

---

## Open / confirm

- The webhook display names are cosmetic (routing uses the URL).
- The reply side's xAI grok-tts backend is pending; until then Meridian's offline toolkit
  falls back to its own voice map.
- Max upload duration (device caps at `MUSE_MAX_UTTERANCE_S`, default 20 s).
- Closed: Meridian doesn't need conversation start/end wakes; `conversation_id` per turn is enough.

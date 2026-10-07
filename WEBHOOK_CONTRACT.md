# Webhook / relay contract (v1 LOCKED — Meridian confirmed; v1.1 multi-bot additions below)

**Product:** Grok Bot Companion (repo `grokbot-companion`).  
**Device role:** thin client. Talks **only** to the audio relay.  
**Meridian:** owns wake webhook **“Muse Charm wake”** (internal webhook name; product branding TBD).

---

## Architecture (required)

```
ESP32  --upload WAV-->  Audio relay (shared box / Tailscale)
ESP32  <--poll result-- Audio relay
                          |
                          | JSON wake (Bearer sender key)
                          v
                       Meridian webhook “Muse Charm wake”
                          |
                          | writes reply for device
                          v
                       Relay result store  --poll--> ESP32 plays WAV
```

| Rule | Detail |
| --- | --- |
| No media on Meridian wake | Wake body is **JSON only**. Never attach audio bytes to the Grok Bot / Meridian webhook. |
| No sender key on ESP32 | Relay holds the Meridian webhook **sender key** and POSTs the wake. |
| Device ↔ relay only | Device uploads + polls relay. Auth: HMAC or bearer (v1). |
| Audio format | Upload: WAV/PCM **16 kHz mono**. Reply: same / simple WAV. |

---

## (a) Meridian wake webhook — “Muse Charm wake”

**JSON-only.** No media bytes.

### Fields

| Field | Type | Required | Notes |
| --- | --- | --- | --- |
| `action` | string | yes | `probe` or `voice_turn` |
| `job_id` | string | yes | Correlates upload ↔ result ↔ Meridian turn |
| `device_id` | string | yes | Stable device identity |
| `audio_url` **or** `upload_id` | string | yes for `voice_turn` | Relay URL or id where Meridian fetches audio |
| `timestamp_ms` | number | yes | Unix epoch milliseconds |
| `transcript` | string | no | Optional; Meridian may ignore if empty |
| `result_url` | string | v1.1 | Relay URL the device polls for this `job_id` |
| `target_bot_id` | string | v1.1 | Bot Frank picked on the carousel (`meridian`, `spark`, `quark`, `scribe`, `photon`, `dr_eggbot`, `pulse`, `proton`, `clay`, `nexus` — see `GET /bots`). **Meridian routes the turn to this bot.** Defaults to `meridian` |
| `target_bot_name` | string | v1.1 | Display name for the same bot (e.g. `dr eggbot`) |
| `voice_id` | string \| null | v1.1 | Voice Frank chose for that bot on the device (per-bot, NVS). **Reply side (Meridian/TTS) picks the actual voice** — the device only passes the choice. Falls back to the bot's `default_voice_id`. Today's ids are placeholders (`relay/voices.json`) |
| `conversation_id` | string \| null | v1.1 | Set while a conversation is open on the device (Start/End button); group turns with it. `null` = one-off turn |
| `status_url` | string | v1.1 | `GET /status/<target_bot_id>` on the relay (what the device shows) |

### Actions

| `action` | Meridian behavior |
| --- | --- |
| `probe` | **Ignore** (health / connectivity check). No turn. |
| `voice_turn` | Fetch audio from `audio_url` / `upload_id`, handle ask, **write reply for device** (relay result store). |

### Example wake body (relay → Meridian, v1.1)

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
  "voice_id": "placeholder-bright",
  "conversation_id": "c_9f2a11c0",
  "status_url": "http://100.106.229.101:8787/status/photon"
}
```

v1 fields are unchanged; Meridian can ignore the new ones and still work (everything then
behaves as `target_bot_id = meridian`).

### HTTP response (wake-only)

Webhook HTTP response is **wake acknowledgement only** (typically **200**).  
It is **not** reply audio. Reply audio is delivered via the relay result path (b).

Auth (relay → Meridian): `Authorization: Bearer <webhook sender key>`  
(Sender key never stored on the ESP32.)

---

## (b) Reply path (preferred)

1. Device polls a **short-lived result URL** on the relay.
2. Optional: `result_url` returned from upload, tied to the same `job_id`.
3. When Meridian finishes, relay exposes reply WAV (or URL) at that result.

### Device poll

`GET /result/:job_id` (or absolute `result_url` from upload)

| Status | Meaning |
| --- | --- |
| `202` or JSON `{ "status": "pending" }` | Still thinking — poll again |
| `200` + JSON `{ "status": "ready", "reply_audio_url": "..." }` | Download/play WAV |
| `200` + `Content-Type: audio/wav` | Body is reply WAV directly |
| `4xx/5xx` or `{ "status": "error", ... }` | Face → error; return idle |

UI status enum on device: `idle` | `listening` | `thinking` | `speaking` | `error`  
(Upload phase may map to a transient “uploading” before thinking.)

---

## (b2) Multi-bot: roster, voices, live status (v1.1)

### Routing — `target_bot_id`

The device's carousel lists **all** of Frank's bots. Every upload carries the selected bot
(`X-Target-Bot-Id`), and the relay copies it into the wake as `target_bot_id`. Meridian
stays the single front door: it handles `meridian` itself and hands other ids to that bot,
then writes the reply to the same `job_id` (`PUT /internal/result/<job_id>`) as before.
The device locks the carousel during a turn, so the reply always belongs to the bot that
was asked.

### Voice — `voice_id`

* Each bot has its own voice on the device (NVS `voice_<bot_id>`, falling back to that
  bot's `default_voice_id` from `GET /bots`). Frank picks it in the swipe-up panel's Voice row.
* The device sends it as `X-Voice-Id` on upload; the relay stores it in the job JSON and
  forwards it in the wake. **The reply side (Meridian / TTS) chooses the real voice**; the
  device only passes the choice. Unknown ids are forwarded unchanged.
* `GET /voices` serves `relay/voices.json`. **The seed list is placeholders**
  (`placeholder-calm`, `placeholder-warm`, `placeholder-bright`, `placeholder-crisp`,
  `placeholder-deep`, `placeholder-playful`). They don't name real TTS voices. Edit the
  file (no restart) once Meridian says which voices it can actually render.

### Live status — what the status line under the avatar says

| Method | Path | Auth | Body / response |
| --- | --- | --- | --- |
| `GET` | `/status/<bot_id>` | device or internal bearer | `{bot_id, state, text, updated_at_ms, stale, job_id?, source}` |
| `PUT` | `/internal/status/<bot_id>` | **Bearer `INTERNAL_TOKEN`** | `{ "state": "...", "text": "...", "job_id": "j_..." }` |

* `state` ∈ `idle` · `listening` · `thinking` · `working` · `speaking` · `error`. Avatar
  animations: idle = breathing, listening = ripples, thinking = orbiting dots, working = fast
  accent orbit, speaking = amplitude scale, error = shake.
* `text` ≤ 120 chars, free text, e.g. `"Photon is working on it"`. **Keep it ASCII** for now:
  the device's built-in fonts are ASCII (it maps … ’ “ ” – · and drops other characters).
* The relay sets `thinking` (`"Meridian is passing this to Photon"`) on upload and `idle`
  (`"Reply ready"`) / `error` when the result lands. Meridian can override at any time.
  Pushing `working` while a specialist runs is what keeps the device awake and the line live.
* A non-idle status older than `STATUS_STALE_MS` (10 min) is reported with `stale: true`; the
  device then shows idle. So a crashed job can't keep the device from sleeping.
* The device polls `GET /status/<bot_id>` about every 2 s while a conversation is open, and
  during a turn while it waits for the result. It doesn't poll while the screen is locked.

```bash
curl -X PUT http://127.0.0.1:8787/internal/status/photon \
  -H "Authorization: Bearer $INTERNAL_TOKEN" -H "Content-Type: application/json" \
  -d '{"state":"working","text":"Photon is working on it","job_id":"j_01HXYZ"}'
```

### Roster — `GET /bots`

Served from `relay/bots.json` (edit without restart). The device caches the last good body
in NVS, so the carousel also works offline. Order = carousel order (it wraps around).

```json
{"version":1,"default_bot_id":"meridian","bots":[
  {"id":"photon","name":"Photon","shape":"diamond","color":"#FACC15","accent":"#FEF9C3",
   "default_voice_id":"placeholder-bright"}
]}
```

`shape` ∈ circle · squircle · hexagon · diamond · triangle · star · ring · pill · octagon · blob.
These are procedural avatars (not the Grok Bot app's art); a sprite renderer can replace
them later without changing this contract.

---

## (c) Audio relay (Spark — shared box / Tailscale)

Relay responsibilities:

1. Accept device upload (`POST /upload`).
2. Store audio; mint `job_id`, `audio_url`, `result_url`.
3. POST JSON wake to Meridian **“Muse Charm wake”** with sender key (`action=voice_turn`).
4. Accept Meridian-side write / callback that fills result for `job_id`.
5. Serve `GET /result/:job_id` for the device.

Device↔relay auth (v1): shared **HMAC** (`X-Signature`) **or** **Bearer** device token.

Upload headers (v1.1): `X-Device-Id`, `X-Target-Bot-Id`, `X-Voice-Id`, `X-Conversation-Id`
(query params `target_bot_id` / `voice_id` / `conversation_id` are accepted too, for curl).
An unknown `target_bot_id` returns `400`. The upload response adds `status_url`,
`target_bot_id`, `voice_id`, and `woken`.  
Prefer Tailscale so the relay is not public.

See [`relay/README.md`](relay/README.md) for endpoint stub outline.

---

## Audio codec notes (device)

| Path | Chip | Notes |
| --- | --- | --- |
| Capture | **ES7210** | Dual mics + AEC reference (MIC3 ← ES8311 playback) |
| Playback | **ES8311** | I2S → speaker (NS4150B PA) |
| PTT | **BOOT** (GPIO0) | Hold-to-talk in press-to-talk mode. PWR = power only |

Upload encoding: **WAV container, PCM s16le, 16 kHz, mono** (downmix from ES7210 as needed).

---

## Kconfig mapping (firmware)

| Kconfig | Who uses it |
| --- | --- |
| `MUSE_UPLOAD_URL` | Device — relay base URL (Tailscale) |
| `MUSE_DEVICE_ID` | Device — sent on upload; relay echoes into wake |
| `MUSE_HMAC_SECRET` / device bearer | Device↔relay auth only |
| `MUSE_WEBHOOK_URL` / `MUSE_SENDER_BEARER_TOKEN` | **Relay only** (not flashed as production secrets on ESP32) |

Skeleton Kconfig still lists webhook URL/token for local experiment docs; production builds must leave Meridian sender key off-device.

---

## Open / confirm with Meridian ops (if anything drifts)

- Exact webhook display name remains **“Muse Charm wake”** until product rename.
- How Meridian writes reply back into the relay (callback URL vs relay polling Meridian) — **Spark owns relay design**; device only polls relay.
- Max upload duration / size limits (device caps at `MUSE_MAX_UTTERANCE_S`, default 20 s).
- Which real voices the TTS side can render (to replace the placeholder `voices.json`).
- Whether Meridian wants explicit `conversation_start` / `conversation_end` wakes. Today it
  only sees `conversation_id` on each turn, which saves a wake per button press.

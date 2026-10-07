# Webhook / relay contract (LOCKED — Meridian confirmed)

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

### Actions

| `action` | Meridian behavior |
| --- | --- |
| `probe` | **Ignore** (health / connectivity check). No turn. |
| `voice_turn` | Fetch audio from `audio_url` / `upload_id`, handle ask, **write reply for device** (relay result store). |

### Example wake body (relay → Meridian)

```json
{
  "action": "voice_turn",
  "job_id": "j_01HXYZ",
  "device_id": "pocket-001",
  "audio_url": "https://relay.example.ts.net/audio/j_01HXYZ.wav",
  "timestamp_ms": 1728262800000,
  "transcript": null
}
```

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

## (c) Audio relay (Spark — shared box / Tailscale)

Relay responsibilities:

1. Accept device upload (`POST /upload`).
2. Store audio; mint `job_id`, `audio_url`, `result_url`.
3. POST JSON wake to Meridian **“Muse Charm wake”** with sender key (`action=voice_turn`).
4. Accept Meridian-side write / callback that fills result for `job_id`.
5. Serve `GET /result/:job_id` for the device.

Device↔relay auth (v1): shared **HMAC** (`X-Signature`) **or** **Bearer** device token.  
Prefer Tailscale so the relay is not public.

See [`relay/README.md`](relay/README.md) for endpoint stub outline.

---

## Audio codec notes (device)

| Path | Chip | Notes |
| --- | --- | --- |
| Capture | **ES7210** | Dual mics + AEC reference (MIC3 ← ES8311 playback) |
| Playback | **ES8311** | I2S → speaker (NS4150B PA) |
| PTT | **BOOT** (GPIO0) or **PWR** (via TCA9554 EXIO4) | v1: BOOT |

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
- Max upload duration / size limits.

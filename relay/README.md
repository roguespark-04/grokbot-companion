# Grok Bot Companion: audio relay (v1.2: direct per-bot wakes, public HTTPS)

**Product:** Grok Bot Companion (repo `grokbot-companion`)  
**Role:** Device-facing upload + result poll on the shared box. The device reaches it over public HTTPS via Tailscale Funnel (`https://grokbot-box.tail4de099.ts.net` → `127.0.0.1:8787`) with the device token; bots/Meridian use it on the box with the internal token. Holds every webhook key and POSTs JSON-only wakes.

Device talks **only** to this relay. Never flash `MERIDIAN_SENDER_KEY` onto the ESP32.

## Quick start

```bash
cd relay
cp .env.example .env
# Edit .env: set PUBLIC_BASE_URL to your Tailscale IP/host, e.g. http://100.x.x.x:8787
# Optionally set MERIDIAN_WEBHOOK_URL + MERIDIAN_SENDER_KEY when Meridian is r## Endpoints

| Method | Path | Auth | Notes |
| --- | --- | --- | --- |
| `GET` | `/health` | none | `{ok:true}` |
| `POST` | `/upload` | device | WAV 16 kHz mono. Headers `X-Device-Id`, `X-Target-Bot-Id` (unknown → `400`, also when `bots.json` is empty), `X-Voice-Id`, `X-Conversation-Id`. Returns `201` `{job_id, audio_url, result_url, status_url, expires_at_ms, target_bot_id, voice_id, wake_route, woken}` |
| `GET` | `/result/<job_id>` | device | `202` pending, `200` ready/error, `410` expired, `404` unknown |
| `GET` | `/replies/<job_id>.wav` | device or internal | Reply WAV (`410` once expired) |
| `GET` | `/audio/<job_id>.wav` | **internal** | The upload, for the bot that answers (Meridian's `transcribe` retries with the internal bearer on 401) |
| `GET` | `/bots` | device or internal | Roster (palette names resolved to hex) |
| `GET` | `/voices` | device or internal | The 28 Grok Bot app voices (`placeholder:false`); `sample_path` only where a clip exists |
| `GET` | `/voices/<id>/sample.wav` | device or internal | Optional/TODO preview clip from `VOICE_SAMPLES_DIR` |
| `GET` | `/status/<bot_id>` | device or internal | `{bot_id,state,text,updated_at_ms,stale,job_id?,source}` |
| `PUT` | `/internal/status/<bot_id>` | internal | `{state,text,job_id?}`; text forced to ASCII, ≤ 120 chars |
| `PUT` | `/internal/result/<job_id>` | internal | `audio/wav` (validated RIFF/WAVE PCM, ≤ 4 MiB else `413`) or JSON with `status` = `ready`/`error` (only `status` decides) |
| `POST` | `/probe[?bot=<id>]` | device or internal | Sends `action=probe` down that bot's wake route |
| `GET` | `/internal/wake-config` | internal | `{meridian_configured, bots:[{bot_id, route, webhook_host}]}`, never keys |

### Wakes: direct per bot, Meridian as fallback

A `voice_turn` for bot X goes to `WAKE_URL_<X>` with `WAKE_KEY_<X>` (upper-cased id,
non-alphanumerics → `_`, e.g. `WAKE_URL_DR_EGGBOT`) when both are set: `wake_route=direct`.
Otherwise it goes to `MERIDIAN_WEBHOOK_URL` / `MERIDIAN_SENDER_KEY`, which routes by
`target_bot_id`: `wake_route=meridian_fallback`. Meridian's own turns use its entry directly.
Headers `Authorization: Bearer <key>` + `X-Automation-Key: <key>`, JSON body, 8 s, one try.
Failures go to `data/wake_fail.log` with `bot_id` and `route`; the device still gets `201`.
`wake_route` is stored in the job JSON. The relay re-reads `.env` on every wake.

```bash
# add / change a bot's direct webhook (key comes from an env var, never printed)
read -rs PHOTON_KEY && export PHOTON_KEY
./set-bot-webhook.sh photon https://<webhook-url> PHOTON_KEY
unset PHOTON_KEY
```

The script writes `WAKE_URL_PHOTON` / `WAKE_KEY_PHOTON` into `.env` (mode 600) and asks the
running relay (`GET /internal/wake-config`) which route it now sees.

### Jobs: TTL and cleanup

Jobs expire after `JOB_TTL_MS` (5 min); a landed reply extends that by `REPLY_TTL_MS`
(5 min) so the device can download it. Expired jobs answer `410`, and a background thread
(every `CLEANUP_INTERVAL_S`) deletes job JSON + upload + reply WAVs `JOB_RETAIN_MS` (1 h)
after expiry.

## Bot roster & voices (no restart needed)

* `bots.json` v2: carousel order + each bot's app look under `avatar` (`shape`: blob ·
  teardrop · cloud · hex · squircle · circle; `color`: a `palette` name or `#RRGGBB`;
  optional `accent`, `rim`, `scale`, `rotation`, `wobble`, `seed`, `tbd`) + `default_voice_id`.
  Tune hex values in `palette` once app screenshots are in. Spark and Quark are `tbd`.
* `voices.json`: the Grok Bot app's 28 voices (xAI grok-tts ids). Clay defaults to `cosmo`,
  the rest to `eve`. The reply side should synthesize with xAI grok-tts using `voice_id`
  (backend pending).
* Both files are re-read on mtime change; the device refreshes on connect and every 15 min.

thorization: Bearer $DEVICE_TOKEN"
```

Keep `text` ASCII and ≤ 120 chars. A non-idle status older than `STATUS_STALE_MS` (default 10 min)
comes back `stale: true`, and the device treats it as idle.

## Config (`.env`)

| Variable | Purpose |
| --- | --- |
| `PUBLIC_BASE_URL` | Absolute base used in `audio_url` / `result_url` / `reply_audio_url` (Tailscale URL) |
| `RELAY_BIND` / `RELAY_PORT` | Listen address (default `0.0.0.0:8787`) |
| `MERIDIAN_WEBHOOK_URL` / `MERIDIAN_SENDER_KEY` | Meridian's companion wake webhook (its own turns + fallback for bots without a direct webhook) |
| `WAKE_URL_<BOT>` / `WAKE_KEY_<BOT>` | Direct wake webhook per bot (see `set-bot-webhook.sh`) |
| `JOB_TTL_MS` / `REPLY_TTL_MS` / `JOB_RETAIN_MS` / `CLEANUP_INTERVAL_S` | Job expiry + cleanup |
| `MAX_REPLY_BYTES` | Reply WAV cap (default 4 MiB) |
| `VOICE_SAMPLES_DIR` | Optional preview clips `<voice_id>.wav` (default `DATA_DIR/voice_samples`) |
| `DEVICE_TOKEN` | Bearer for device upload/poll |
| `DEVICE_HMAC_SECRET` | Optional alternative: `X-Signature: sha256=<hex>` over body |
| `INTERNAL_TOKEN` | Bearer for `PUT /internal/result/...` |
| `DATA_DIR` | Job JSON + audio + replies + `status/` (survives restarts) |
| `BOTS_FILE` / `VOICES_FILE` | Override paths to `bots.json` / `voices.json` (default: next to `server.py`) |
| `STATUS_STALE_MS` | Age after which non-idle status is reported stale (default 600000) |

Secrets live in `relay/.env` only (gitignored). See `.env.example`.

## Layout

```
relay/
  server.py          # Flask relay (v1.1)
  bots.json          # roster: carousel order + avatar shape/color + default voice
  voices.json        # the 28 Grok Bot app voices (GET /voices)
  set-bot-webhook.sh # write WAKE_URL_/WAKE_KEY_ for a bot without printing the key
  stub_server.py     # thin re-export of server.py
  run.sh             # venv + load .env + start
  ensure-up.sh       # restart tailscaled / relay if down (box has no service manager)
  requirements.txt
  .env.example
  data/              # runtime (gitignored contents ok)
    audio/
    replies/
    jobs/
    status/          # one JSON per bot (latest state/text)
    wake_fail.log    # failed wakes for later drain
```

## Network

* **Device:** public HTTPS through Tailscale Funnel, `https://grokbot-box.tail4de099.ts.net`
  (firmware default `MUSE_UPLOAD_URL`), device bearer on every call. The firmware re-bases the
  relay paths it gets back (`/replies/...`) onto that host and sends its token only there.
* **Bots / Meridian:** on the box, `http://127.0.0.1:8787` or `PUBLIC_BASE_URL` (tailnet),
  internal bearer. `PUBLIC_BASE_URL` stays the tailnet address because the bots are local.

## Security notes

- The relay is public via Funnel: every endpoint except `/health` needs a bearer.
  `/audio` is internal-only; `/replies` is device or internal.
- Never log or print `MERIDIAN_SENDER_KEY`, `WAKE_KEY_*`, `DEVICE_TOKEN`, or `INTERNAL_TOKEN`.
- `wake_fail.log` holds wake bodies and errors, never keys.

## Restarting safely (shared box)

```bash
pkill -f "python server.py"          # only the relay process
cd /workspace/grokbot-companion-push/relay
nohup ./run.sh >> /tmp/grokbot-relay.log 2>&1 &
curl -s http://127.0.0.1:8787/health
```

## Manual smoke test

```bash
export DEVICE_TOKEN=...   # from .env
export INTERNAL_TOKEN=...

# Upload
curl -s -D- -X POST http://127.0.0.1:8787/upload \
  -H "Authorization: Bearer $DEVICE_TOKEN" \
  -H "X-Device-Id: pocket-001" \
  -H "Content-Type: audio/wav" \
  --data-binary @sample.wav

# Poll (expect 202 pending until reply written)
curl -s -D- http://127.0.0.1:8787/result/j_... \
  -H "Authorization: Bearer $DEVICE_TOKEN"

# Ops writes reply
curl -s -X PUT http://127.0.0.1:8787/internal/result/j_... \
  -H "Authorization: Bearer $INTERNAL_TOKEN" \
  -H "Content-Type: audio/wav" \
  --data-binary @reply.wav
```

Contract details: [../WEBHOOK_CONTRACT.md](../WEBHOOK_CONTRACT.md).

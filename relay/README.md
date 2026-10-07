# Grok Bot Companion — audio relay (v1.1, multi-bot)

**Product:** Grok Bot Companion (repo `grokbot-companion`)  
**Role:** Device-facing upload + result poll on the shared box (Tailscale). Holds the Meridian webhook sender key and POSTs JSON-only wakes.

Device talks **only** to this relay. Never flash `MERIDIAN_SENDER_KEY` onto the ESP32.

## Quick start

```bash
cd relay
cp .env.example .env
# Edit .env: set PUBLIC_BASE_URL to your Tailscale IP/host, e.g. http://100.x.x.x:8787
# Optionally set MERIDIAN_WEBHOOK_URL + MERIDIAN_SENDER_KEY when Meridian is ready.
./run.sh
```

On first start, blank `DEVICE_TOKEN` / `INTERNAL_TOKEN` are generated into `.env` and printed once.

Health check:

```bash
curl -s http://127.0.0.1:8787/health
# {"ok":true}
```

## Endpoints

| Method | Path | Auth | Notes |
| --- | --- | --- | --- |
| `POST` | `/upload` | Bearer `DEVICE_TOKEN` or `X-Signature` HMAC | Body: WAV/PCM 16 kHz mono. Headers `X-Device-Id`, `X-Target-Bot-Id` (default `meridian`; unknown → `400`), `X-Voice-Id` (default = bot's `default_voice_id`), `X-Conversation-Id` (optional). Returns `201` `{job_id, audio_url, result_url, status_url, expires_at_ms, target_bot_id, voice_id, woken}` |
| `GET` | `/bots` | device or internal bearer | Roster from `bots.json`: `{default_bot_id, bots:[{id,name,shape,color,accent,default_voice_id?}]}` |
| `GET` | `/voices` | device or internal bearer | Placeholder voices from `voices.json`: `{placeholder:true, voices:[{id,name,description}]}` |
| `GET` | `/status/<bot_id>` | device or internal bearer | `{bot_id,state,text,updated_at_ms,stale,job_id?,source}`; unknown bot → `404` |
| `PUT` | `/internal/status/<bot_id>` | Bearer `INTERNAL_TOKEN` | Meridian pushes `{state,text,job_id?}`; `state` ∈ idle/listening/thinking/working/speaking/error |
| `GET` | `/result/<job_id>` | device auth | `202` pending / `200` ready (`reply_audio_url`) or error |
| `GET` | `/audio/<job_id>.wav` | Tailscale / unguessable id | Meridian fetches uploaded audio |
| `GET` | `/replies/<job_id>.wav` | Tailscale / unguessable id | Device downloads reply WAV |
| `PUT` | `/internal/result/<job_id>` | Bearer `INTERNAL_TOKEN` | Meridian/ops writes `audio/wav` or JSON |
| `GET` | `/health` | none | `{ok:true}` |
| `POST` | `/probe` | device or internal | Wakes Meridian with `action=probe` |

### After upload → Meridian wake

```
POST MERIDIAN_WEBHOOK_URL
Authorization: Bearer <MERIDIAN_SENDER_KEY>
X-Automation-Key: <MERIDIAN_SENDER_KEY>
Content-Type: application/json
timeout 8s, one try, no retry

{ "action": "voice_turn", "job_id", "device_id", "audio_url", "result_url", "timestamp_ms",
  "target_bot_id", "target_bot_name", "voice_id", "conversation_id", "status_url" }
```

The job JSON (`data/jobs/<job_id>.json`) also stores `target_bot_id`, `voice_id`, and `conversation_id`.
On upload the relay sets the bot's status to `thinking`. When the result lands it sets
`idle` ("Reply ready") or `error`, unless a newer job already owns that bot's status.

If the wake fails, the relay appends a JSON line to `data/wake_fail.log` and still returns `201` to the device (result stays `pending`). If URL/key are unset, it logs a warning and skips the wake (dev mode).

### Writing a reply (ops / Meridian)

```bash
curl -X PUT "http://127.0.0.1:8787/internal/result/j_abc" \
  -H "Authorization: Bearer $INTERNAL_TOKEN" \
  -H "Content-Type: audio/wav" \
  --data-binary @reply.wav
```

Or JSON: `{ "status": "ready", "reply_audio_url": "..." }` / `{ "status": "error", "message": "..." }`.

## Bot roster & voices (no restart needed)

* `bots.json` is the carousel order plus each bot's procedural avatar (`shape`, `color`, `accent`)
  and optional `default_voice_id`. Invalid rows are skipped and logged.
* `voices.json` holds **placeholder** voice ids. They're labels the device passes through as
  `voice_id`, not real TTS voices. Replace them once Meridian/TTS confirms real ones.
* Both files are re-read when their mtime changes. The device refreshes them on Wi-Fi
  connect and every 15 min, and caches them in NVS.

## Live status

```bash
# Meridian (or ops) pushes what the device should show under the bot's avatar
curl -X PUT http://127.0.0.1:8787/internal/status/photon \
  -H "Authorization: Bearer $INTERNAL_TOKEN" -H "Content-Type: application/json" \
  -d '{"state":"working","text":"Photon is working on it"}'

curl -s http://127.0.0.1:8787/status/photon -H "Authorization: Bearer $DEVICE_TOKEN"
```

Keep `text` ASCII and ≤ 120 chars. A non-idle status older than `STATUS_STALE_MS` (default 10 min)
comes back `stale: true`, and the device treats it as idle.

## Config (`.env`)

| Variable | Purpose |
| --- | --- |
| `PUBLIC_BASE_URL` | Absolute base used in `audio_url` / `result_url` / `reply_audio_url` (Tailscale URL) |
| `RELAY_BIND` / `RELAY_PORT` | Listen address (default `0.0.0.0:8787`) |
| `MERIDIAN_WEBHOOK_URL` / `MERIDIAN_SENDER_KEY` | Meridian “Muse Charm wake” |
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
  voices.json        # PLACEHOLDER voice list (GET /voices)
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

## Tailscale

Prefer Tailscale so the relay is not on the public internet:

1. Note the box Tailscale IPv4 (`tailscale ip -4`) or MagicDNS name.
2. Set `PUBLIC_BASE_URL=http://<that>:8787` (or HTTPS if you terminate TLS via Tailscale Serve).
3. Point firmware `MUSE_UPLOAD_URL` at the same base.
4. Put `DEVICE_TOKEN` (or HMAC secret) on the device NVS — never Meridian’s sender key.

## Security notes

- Bind via Tailscale; avoid exposing `:8787` publicly for v1.
- Job IDs are unguessable; audio/reply GETs rely on that + private network.
- Never log `MERIDIAN_SENDER_KEY`, `DEVICE_TOKEN`, or `INTERNAL_TOKEN`.
- Failed wakes land in `wake_fail.log` (bodies only — no secrets beyond URLs).

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

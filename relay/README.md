# Grok Bot Companion — audio relay (v1)

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
| `POST` | `/upload` | Bearer `DEVICE_TOKEN` or `X-Signature` HMAC | Body: WAV/PCM 16 kHz mono. Header `X-Device-Id`. Returns `201` `{job_id, audio_url, result_url, expires_at_ms}` |
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

{ "action": "voice_turn", "job_id", "device_id", "audio_url", "timestamp_ms" }
```

If the wake fails, the relay appends a JSON line to `data/wake_fail.log` and still returns `201` to the device (result stays `pending`). If URL/key are unset, it logs a warning and skips the wake (dev mode).

### Writing a reply (ops / Meridian)

```bash
curl -X PUT "http://127.0.0.1:8787/internal/result/j_abc" \
  -H "Authorization: Bearer $INTERNAL_TOKEN" \
  -H "Content-Type: audio/wav" \
  --data-binary @reply.wav
```

Or JSON: `{ "status": "ready", "reply_audio_url": "..." }` / `{ "status": "error", "message": "..." }`.

## Config (`.env`)

| Variable | Purpose |
| --- | --- |
| `PUBLIC_BASE_URL` | Absolute base used in `audio_url` / `result_url` / `reply_audio_url` (Tailscale URL) |
| `RELAY_BIND` / `RELAY_PORT` | Listen address (default `0.0.0.0:8787`) |
| `MERIDIAN_WEBHOOK_URL` / `MERIDIAN_SENDER_KEY` | Meridian “Muse Charm wake” |
| `DEVICE_TOKEN` | Bearer for device upload/poll |
| `DEVICE_HMAC_SECRET` | Optional alternative: `X-Signature: sha256=<hex>` over body |
| `INTERNAL_TOKEN` | Bearer for `PUT /internal/result/...` |
| `DATA_DIR` | Job JSON + audio + replies (survives restarts) |

Secrets live in `relay/.env` only (gitignored). See `.env.example`.

## Layout

```
relay/
  server.py          # Flask v1 relay
  stub_server.py     # thin re-export of server.py
  run.sh             # venv + load .env + start
  requirements.txt
  .env.example
  data/              # runtime (gitignored contents ok)
    audio/
    replies/
    jobs/
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

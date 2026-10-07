# Audio relay (stub outline)

**Owner:** Spark — runs on shared box (Tailscale).  
**Role:** Device-facing upload + result poll; holds Meridian webhook sender key; POSTs JSON-only wake.

Product name TBD (pocket companion). Do not brand public URLs as “muse-charm” until Frank picks a name.

## Endpoints (v1)

### `POST /upload`

- **Auth:** device HMAC (`X-Signature`) or Bearer (v1).
- **Body:** `audio/wav` (PCM 16 kHz mono) — raw or multipart field `audio`.
- **Headers (suggested):** `X-Device-Id`, `Content-Type: audio/wav`
- **Response `201` JSON:**

```json
{
  "job_id": "j_01HXYZ",
  "audio_url": "https://<relay-tailscale>/audio/j_01HXYZ.wav",
  "result_url": "https://<relay-tailscale>/result/j_01HXYZ",
  "expires_at_ms": 1728263100000
}
```

After accept: relay POSTs Meridian wake (`action=voice_turn`, fields per WEBHOOK_CONTRACT.md) using the **sender key stored only here**.

### `GET /result/:job_id`

- **Auth:** same device↔relay scheme.
- **Pending:** `202` or `200` + `{ "status": "pending", "job_id": "..." }`
- **Ready:** `200` + `{ "status": "ready", "reply_audio_url": "https://.../replies/j_01HXYZ.wav" }`  
  or `200` with `Content-Type: audio/wav` body.
- **Error:** `{ "status": "error", "message": "..." }`

### Internal: wake → Meridian

Not exposed to the device.

```
POST <Meridian webhook URL for “Muse Charm wake”>
Authorization: Bearer <SENDER_KEY>
Content-Type: application/json

{ "action": "voice_turn", "job_id", "device_id", "audio_url"| "upload_id", "timestamp_ms", "transcript"? }
```

Expect wake-only HTTP 200. Reply path is relay result store, not webhook body.

### Optional: Meridian → relay result write

```
PUT /internal/result/:job_id
Authorization: <service auth>
Body: audio/wav  OR  JSON { "reply_audio_url": "..." }
```

(Exact callback shape is Spark’s choice; device never sees this.)

## Stub layout (this folder)

```
relay/
  README.md          ← this file
  stub_server.py     ← minimal FastAPI/Flask outline (optional local mock)
  .env.example       ← MERIDIAN_WEBHOOK_URL, MERIDIAN_SENDER_KEY, DEVICE_HMAC_SECRET
```

## Security

- Bind to Tailscale interface when possible.
- Short-lived `job_id` / result URLs.
- Never log sender key or full device secrets.
- Device firmware must not embed `MERIDIAN_SENDER_KEY`.

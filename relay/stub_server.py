#!/usr/bin/env python3
"""Minimal relay stub outline — not production.

Endpoints:
  POST /upload
  GET  /result/<job_id>
  (internal wake to Meridian after upload)

Run (dev):  pip install flask requests && python stub_server.py
"""
from __future__ import annotations

import os
import time
import uuid
from pathlib import Path

# Optional deps — stub prints instructions if missing
try:
    from flask import Flask, request, jsonify
    import requests
except ImportError:
    print("Install: pip install flask requests")
    raise SystemExit(1)

app = Flask(__name__)
STORE = Path("/tmp/pocket-relay-stub")
STORE.mkdir(parents=True, exist_ok=True)
RESULTS: dict[str, dict] = {}

WEBHOOK = os.environ.get("MERIDIAN_WEBHOOK_URL", "")
SENDER = os.environ.get("MERIDIAN_SENDER_KEY", "")


@app.post("/upload")
def upload():
    device_id = request.headers.get("X-Device-Id", "unknown")
    job_id = "j_" + uuid.uuid4().hex[:12]
    raw = request.get_data()
    wav_path = STORE / f"{job_id}.wav"
    wav_path.write_bytes(raw)
    audio_url = f"http://127.0.0.1:8787/audio/{job_id}.wav"
    result_url = f"http://127.0.0.1:8787/result/{job_id}"
    RESULTS[job_id] = {"status": "pending", "device_id": device_id}

    # Internal: JSON-only wake to Meridian (sender key stays here)
    if WEBHOOK and SENDER:
        body = {
            "action": "voice_turn",
            "job_id": job_id,
            "device_id": device_id,
            "audio_url": audio_url,
            "timestamp_ms": int(time.time() * 1000),
        }
        requests.post(
            WEBHOOK,
            json=body,
            headers={"Authorization": f"Bearer {SENDER}"},
            timeout=30,
        )
    else:
        print("WARN: MERIDIAN_WEBHOOK_URL / MERIDIAN_SENDER_KEY unset — skip wake")

    return jsonify(
        job_id=job_id,
        audio_url=audio_url,
        result_url=result_url,
        expires_at_ms=int(time.time() * 1000) + 300_000,
    ), 201


@app.get("/audio/<job_id>.wav")
def audio(job_id: str):
    path = STORE / f"{job_id}.wav"
    if not path.exists():
        return jsonify(error="not found"), 404
    return path.read_bytes(), 200, {"Content-Type": "audio/wav"}


@app.get("/result/<job_id>")
def result(job_id: str):
    row = RESULTS.get(job_id)
    if not row:
        return jsonify(status="error", message="unknown job"), 404
    if row["status"] == "pending":
        return jsonify(status="pending", job_id=job_id), 202
    return jsonify(row), 200


@app.put("/internal/result/<job_id>")
def put_result(job_id: str):
    """Meridian/ops writes reply — not called by device."""
    if job_id not in RESULTS:
        return jsonify(error="unknown job"), 404
    if request.content_type and "audio" in request.content_type:
        out = STORE / f"{job_id}-reply.wav"
        out.write_bytes(request.get_data())
        RESULTS[job_id] = {
            "status": "ready",
            "reply_audio_url": f"http://127.0.0.1:8787/replies/{job_id}.wav",
            "job_id": job_id,
        }
    else:
        data = request.get_json(force=True, silent=True) or {}
        RESULTS[job_id] = {"status": "ready", "job_id": job_id, **data}
    return jsonify(ok=True)


@app.get("/replies/<job_id>.wav")
def replies(job_id: str):
    path = STORE / f"{job_id}-reply.wav"
    if not path.exists():
        return jsonify(error="not found"), 404
    return path.read_bytes(), 200, {"Content-Type": "audio/wav"}


if __name__ == "__main__":
    app.run(host=os.environ.get("RELAY_BIND", "0.0.0.0"),
            port=int(os.environ.get("RELAY_PORT", "8787")))

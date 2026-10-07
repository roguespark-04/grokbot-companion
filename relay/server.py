#!/usr/bin/env python3
"""Grok Bot Companion — audio relay (v1).

Device talks only to this relay. Meridian sender key stays here.
Persists job metadata under DATA_DIR so restarts keep pending jobs.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import logging
import os
import secrets
import sys
import time
import uuid
from pathlib import Path

from dotenv import load_dotenv
from flask import Flask, jsonify, request, send_file
import requests

# ---------------------------------------------------------------------------
# Paths & env
# ---------------------------------------------------------------------------

RELAY_DIR = Path(__file__).resolve().parent
ENV_PATH = RELAY_DIR / ".env"
load_dotenv(ENV_PATH)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("relay")

PUBLIC_BASE_URL = os.environ.get("PUBLIC_BASE_URL", "http://127.0.0.1:8787").rstrip("/")
RELAY_BIND = os.environ.get("RELAY_BIND", "0.0.0.0")
RELAY_PORT = int(os.environ.get("RELAY_PORT", "8787"))
MERIDIAN_WEBHOOK_URL = os.environ.get("MERIDIAN_WEBHOOK_URL", "").strip()
MERIDIAN_SENDER_KEY = os.environ.get("MERIDIAN_SENDER_KEY", "").strip()
DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "").strip()
DEVICE_HMAC_SECRET = os.environ.get("DEVICE_HMAC_SECRET", "").strip()
INTERNAL_TOKEN = os.environ.get("INTERNAL_TOKEN", "").strip()
DATA_DIR = Path(
    os.environ.get("DATA_DIR", str(RELAY_DIR / "data"))
).resolve()
JOB_TTL_MS = int(os.environ.get("JOB_TTL_MS", str(300_000)))  # 5 min default
MAX_UPLOAD_BYTES = int(os.environ.get("MAX_UPLOAD_BYTES", str(5 * 1024 * 1024)))

AUDIO_DIR = DATA_DIR / "audio"
REPLIES_DIR = DATA_DIR / "replies"
JOBS_DIR = DATA_DIR / "jobs"
WAKE_FAIL_LOG = DATA_DIR / "wake_fail.log"


def _ensure_dirs() -> None:
    for d in (DATA_DIR, AUDIO_DIR, REPLIES_DIR, JOBS_DIR):
        d.mkdir(parents=True, exist_ok=True)


def _generate_tokens_if_missing() -> list[str]:
    """Write DEVICE_TOKEN / INTERNAL_TOKEN into .env when absent. Returns names generated."""
    generated: list[str] = []
    # Read existing .env lines (preserve comments / other keys)
    existing: dict[str, str] = {}
    lines: list[str] = []
    if ENV_PATH.exists():
        raw = ENV_PATH.read_text(encoding="utf-8")
        lines = raw.splitlines(keepends=True)
        for line in raw.splitlines():
            s = line.strip()
            if not s or s.startswith("#") or "=" not in s:
                continue
            k, _, v = s.partition("=")
            existing[k.strip()] = v.strip()

    updates: dict[str, str] = {}
    if not (DEVICE_TOKEN or existing.get("DEVICE_TOKEN", "").strip()):
        updates["DEVICE_TOKEN"] = secrets.token_urlsafe(32)
        generated.append("DEVICE_TOKEN")
    if not (INTERNAL_TOKEN or existing.get("INTERNAL_TOKEN", "").strip()):
        updates["INTERNAL_TOKEN"] = secrets.token_urlsafe(32)
        generated.append("INTERNAL_TOKEN")

    if not updates:
        return generated

    # Rewrite .env: replace keys if present, else append
    out_lines: list[str] = []
    seen: set[str] = set()
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("#") and "=" in stripped:
            k = stripped.partition("=")[0].strip()
            if k in updates:
                out_lines.append(f"{k}={updates[k]}\n")
                seen.add(k)
                continue
        out_lines.append(line if line.endswith("\n") else line + "\n")
    for k, v in updates.items():
        if k not in seen:
            out_lines.append(f"{k}={v}\n")

    ENV_PATH.write_text("".join(out_lines), encoding="utf-8")
    # Refresh process env so this process uses the new values
    for k, v in updates.items():
        os.environ[k] = v
    return generated


def _refresh_auth_from_env() -> None:
    global DEVICE_TOKEN, INTERNAL_TOKEN, DEVICE_HMAC_SECRET
    global MERIDIAN_WEBHOOK_URL, MERIDIAN_SENDER_KEY
    load_dotenv(ENV_PATH, override=True)
    DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "").strip()
    INTERNAL_TOKEN = os.environ.get("INTERNAL_TOKEN", "").strip()
    DEVICE_HMAC_SECRET = os.environ.get("DEVICE_HMAC_SECRET", "").strip()
    MERIDIAN_WEBHOOK_URL = os.environ.get("MERIDIAN_WEBHOOK_URL", "").strip()
    MERIDIAN_SENDER_KEY = os.environ.get("MERIDIAN_SENDER_KEY", "").strip()


# ---------------------------------------------------------------------------
# Job persistence
# ---------------------------------------------------------------------------

def _job_path(job_id: str) -> Path:
    # Sanitize: only allow our job_id alphabet
    safe = "".join(c for c in job_id if c.isalnum() or c in "_-")
    if safe != job_id or not job_id:
        raise ValueError("invalid job_id")
    return JOBS_DIR / f"{job_id}.json"


def save_job(job: dict) -> None:
    path = _job_path(job["job_id"])
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(job, indent=2), encoding="utf-8")
    tmp.replace(path)


def load_job(job_id: str) -> dict | None:
    try:
        path = _job_path(job_id)
    except ValueError:
        return None
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        log.warning("corrupt job file %s: %s", path, e)
        return None


def new_job_id() -> str:
    return "j_" + uuid.uuid4().hex[:12]


# ---------------------------------------------------------------------------
# Auth helpers
# ---------------------------------------------------------------------------

def _bearer_token() -> str | None:
    auth = request.headers.get("Authorization", "")
    if auth.lower().startswith("bearer "):
        return auth[7:].strip()
    return None


def require_device_auth() -> tuple | None:
    """Return Flask error response if device auth fails, else None."""
    if not DEVICE_TOKEN and not DEVICE_HMAC_SECRET:
        log.warning("DEVICE_TOKEN and DEVICE_HMAC_SECRET both unset — rejecting")
        return jsonify(error="server misconfigured: no device auth"), 503

    token = _bearer_token()
    if DEVICE_TOKEN and token and hmac.compare_digest(token, DEVICE_TOKEN):
        return None

    sig = request.headers.get("X-Signature", "").strip()
    if DEVICE_HMAC_SECRET and sig:
        body = request.get_data()
        expected = hmac.new(
            DEVICE_HMAC_SECRET.encode("utf-8"),
            body,
            hashlib.sha256,
        ).hexdigest()
        # Accept raw hex or sha256=<hex>
        provided = sig.split("=", 1)[-1] if sig.lower().startswith("sha256=") else sig
        if hmac.compare_digest(provided.lower(), expected.lower()):
            return None

    return jsonify(error="unauthorized"), 401


def require_internal_auth() -> tuple | None:
    if not INTERNAL_TOKEN:
        log.warning("INTERNAL_TOKEN unset — rejecting internal write")
        return jsonify(error="server misconfigured: no internal auth"), 503
    token = _bearer_token()
    if token and hmac.compare_digest(token, INTERNAL_TOKEN):
        return None
    return jsonify(error="unauthorized"), 401


# ---------------------------------------------------------------------------
# Meridian wake
# ---------------------------------------------------------------------------

def wake_meridian(body: dict) -> bool:
    """POST JSON wake once. On failure append to wake_fail.log. Returns True if sent."""
    if not MERIDIAN_WEBHOOK_URL or not MERIDIAN_SENDER_KEY:
        log.warning(
            "MERIDIAN_WEBHOOK_URL / MERIDIAN_SENDER_KEY unset — skip wake (dev mode)"
        )
        return False

    headers = {
        "Authorization": f"Bearer {MERIDIAN_SENDER_KEY}",
        "X-Automation-Key": MERIDIAN_SENDER_KEY,
        "Content-Type": "application/json",
    }
    try:
        resp = requests.post(
            MERIDIAN_WEBHOOK_URL,
            json=body,
            headers=headers,
            timeout=8,
        )
        if resp.status_code >= 400:
            raise RuntimeError(f"HTTP {resp.status_code}: {resp.text[:200]}")
        log.info(
            "wake ok action=%s job_id=%s status=%s",
            body.get("action"),
            body.get("job_id"),
            resp.status_code,
        )
        return True
    except Exception as e:
        log.error(
            "wake failed action=%s job_id=%s: %s",
            body.get("action"),
            body.get("job_id"),
            e,
        )
        _append_wake_fail(body, str(e))
        return False


def _append_wake_fail(body: dict, error: str) -> None:
    record = {
        "failed_at_ms": int(time.time() * 1000),
        "error": error,
        "body": body,
    }
    try:
        with WAKE_FAIL_LOG.open("a", encoding="utf-8") as f:
            f.write(json.dumps(record) + "\n")
    except OSError as e:
        log.error("could not write wake_fail.log: %s", e)


# ---------------------------------------------------------------------------
# Flask app
# ---------------------------------------------------------------------------

app = Flask(__name__)


@app.get("/health")
def health():
    return jsonify(ok=True), 200


@app.post("/upload")
def upload():
    err = require_device_auth()
    if err:
        return err

    device_id = request.headers.get("X-Device-Id", "unknown").strip() or "unknown"
    raw = request.get_data()
    if not raw:
        return jsonify(error="empty body"), 400
    if len(raw) > MAX_UPLOAD_BYTES:
        return jsonify(error="upload too large"), 413

    job_id = new_job_id()
    now_ms = int(time.time() * 1000)
    expires_at_ms = now_ms + JOB_TTL_MS

    audio_path = AUDIO_DIR / f"{job_id}.wav"
    audio_path.write_bytes(raw)

    audio_url = f"{PUBLIC_BASE_URL}/audio/{job_id}.wav"
    result_url = f"{PUBLIC_BASE_URL}/result/{job_id}"

    job = {
        "job_id": job_id,
        "device_id": device_id,
        "status": "pending",
        "created_at_ms": now_ms,
        "expires_at_ms": expires_at_ms,
        "audio_url": audio_url,
        "result_url": result_url,
        "audio_path": str(audio_path),
        "reply_audio_url": None,
        "error": None,
    }
    save_job(job)

    wake_body = {
        "action": "voice_turn",
        "job_id": job_id,
        "device_id": device_id,
        "audio_url": audio_url,
        "timestamp_ms": now_ms,
    }
    wake_meridian(wake_body)
    # Always 201 with pending result even if wake failed

    return (
        jsonify(
            job_id=job_id,
            audio_url=audio_url,
            result_url=result_url,
            expires_at_ms=expires_at_ms,
        ),
        201,
    )


@app.get("/result/<job_id>")
def get_result(job_id: str):
    err = require_device_auth()
    if err:
        return err

    job = load_job(job_id)
    if not job:
        return jsonify(status="error", message="unknown job"), 404

    status = job.get("status", "pending")
    if status == "pending":
        return jsonify(status="pending", job_id=job_id), 202
    if status == "error":
        return (
            jsonify(
                status="error",
                job_id=job_id,
                message=job.get("error") or "error",
            ),
            200,
        )
    # ready
    payload = {
        "status": "ready",
        "job_id": job_id,
        "reply_audio_url": job.get("reply_audio_url"),
    }
    # Pass through any extra fields Meridian wrote via JSON
    for k, v in job.items():
        if k not in (
            "job_id",
            "device_id",
            "status",
            "created_at_ms",
            "expires_at_ms",
            "audio_url",
            "result_url",
            "audio_path",
            "reply_path",
            "error",
            "reply_audio_url",
        ):
            payload[k] = v
    return jsonify(payload), 200


@app.get("/audio/<job_id>.wav")
def get_audio(job_id: str):
    # Meridian fetches this; protect lightly — require either internal or device auth,
    # or allow if INTERNAL/DEVICE tokens match. For v1 Meridian may use INTERNAL_TOKEN
    # or fetch over Tailscale without auth. Prefer: allow with internal bearer OR
    # device bearer; if neither configured token present and no Authorization, still
    # serve on Tailscale (audio URLs are unguessable job_ids). Documented tradeoff.
    path = AUDIO_DIR / f"{job_id}.wav"
    # Validate job_id alphabet
    try:
        _job_path(job_id)
    except ValueError:
        return jsonify(error="not found"), 404
    if not path.exists():
        return jsonify(error="not found"), 404
    return send_file(path, mimetype="audio/wav", as_attachment=False)


@app.get("/replies/<job_id>.wav")
def get_reply(job_id: str):
    try:
        _job_path(job_id)
    except ValueError:
        return jsonify(error="not found"), 404
    path = REPLIES_DIR / f"{job_id}.wav"
    if not path.exists():
        return jsonify(error="not found"), 404
    return send_file(path, mimetype="audio/wav", as_attachment=False)


@app.put("/internal/result/<job_id>")
def put_result(job_id: str):
    err = require_internal_auth()
    if err:
        return err

    job = load_job(job_id)
    if not job:
        return jsonify(error="unknown job"), 404

    ctype = (request.content_type or "").lower()
    if "audio" in ctype or (request.get_data() and not ctype.startswith("application/json")):
        # Prefer audio when Content-Type says so; also accept raw wav without JSON ctype
        raw = request.get_data()
        if not raw:
            return jsonify(error="empty audio body"), 400
        reply_path = REPLIES_DIR / f"{job_id}.wav"
        reply_path.write_bytes(raw)
        job["status"] = "ready"
        job["reply_audio_url"] = f"{PUBLIC_BASE_URL}/replies/{job_id}.wav"
        job["reply_path"] = str(reply_path)
        job["error"] = None
        save_job(job)
        return jsonify(ok=True, job_id=job_id, status="ready"), 200

    data = request.get_json(force=True, silent=True) or {}
    if data.get("status") == "error" or data.get("error") or data.get("message"):
        job["status"] = "error"
        job["error"] = (
            data.get("message")
            or data.get("error")
            or "error"
        )
        save_job(job)
        return jsonify(ok=True, job_id=job_id, status="error"), 200

    # JSON ready: may include reply_audio_url or we expect audio was already written
    if "reply_audio_url" in data:
        job["reply_audio_url"] = data["reply_audio_url"]
    # If JSON includes base64 or similar we ignore for v1 — Meridian should PUT audio/wav
    for k, v in data.items():
        if k in ("status", "job_id"):
            continue
        if k not in job or k in ("reply_audio_url", "transcript", "text"):
            job[k] = v
    job["status"] = data.get("status") or "ready"
    if job["status"] == "ready" and not job.get("reply_audio_url"):
        # Check if reply file already exists
        reply_path = REPLIES_DIR / f"{job_id}.wav"
        if reply_path.exists():
            job["reply_audio_url"] = f"{PUBLIC_BASE_URL}/replies/{job_id}.wav"
            job["reply_path"] = str(reply_path)
    save_job(job)
    return jsonify(ok=True, job_id=job_id, status=job["status"]), 200


@app.post("/probe")
def probe():
    """Optional: wake Meridian with action=probe to health-check the wake path."""
    err = require_device_auth()
    if err:
        # Also allow internal token for ops probes
        ierr = require_internal_auth()
        if ierr:
            return err  # prefer device auth error message shape

    device_id = request.headers.get("X-Device-Id", "probe").strip() or "probe"
    now_ms = int(time.time() * 1000)
    job_id = "probe_" + uuid.uuid4().hex[:8]
    body = {
        "action": "probe",
        "job_id": job_id,
        "device_id": device_id,
        "timestamp_ms": now_ms,
    }
    ok = wake_meridian(body)
    return jsonify(ok=ok, job_id=job_id, woken=ok), 200 if ok else 503


def main() -> None:
    _ensure_dirs()
    generated = _generate_tokens_if_missing()
    _refresh_auth_from_env()
    if generated:
        print(
            "Generated and wrote to relay/.env (printed once): "
            + ", ".join(generated)
        )
        for name in generated:
            # Print values once so operator can copy; never print MERIDIAN_SENDER_KEY
            print(f"  {name}={os.environ.get(name, '')}")
    if not MERIDIAN_WEBHOOK_URL or not MERIDIAN_SENDER_KEY:
        log.warning(
            "dev mode: MERIDIAN_WEBHOOK_URL and/or MERIDIAN_SENDER_KEY unset — wakes skipped"
        )
    if not DEVICE_TOKEN and not DEVICE_HMAC_SECRET:
        log.error("no DEVICE_TOKEN or DEVICE_HMAC_SECRET — uploads will fail")
    if not INTERNAL_TOKEN:
        log.error("no INTERNAL_TOKEN — internal result writes will fail")

    log.info(
        "Grok Bot Companion relay listening on %s:%s  PUBLIC_BASE_URL=%s  DATA_DIR=%s",
        RELAY_BIND,
        RELAY_PORT,
        PUBLIC_BASE_URL,
        DATA_DIR,
    )
    app.run(host=RELAY_BIND, port=RELAY_PORT, threaded=True)


if __name__ == "__main__":
    main()

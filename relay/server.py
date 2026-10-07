#!/usr/bin/env python3
"""Grok Bot Companion — audio relay (v1.1: multi-bot).

Device talks only to this relay. Meridian sender key stays here.
Persists job metadata under DATA_DIR so restarts keep pending jobs.

v1.1 adds the multi-bot roster (GET /bots from bots.json), placeholder voice
list (GET /voices from voices.json), per-bot live status (GET /status/<bot_id>,
PUT /internal/status/<bot_id>), and target_bot_id / voice_id / conversation_id
on uploads + the JSON wake so Meridian can route each turn.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import logging
import os
import re
import struct
import unicodedata
import secrets
import sys
import threading
import time
import uuid
from pathlib import Path

from dotenv import dotenv_values, load_dotenv
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
# After a reply lands the device still needs time to download it.
REPLY_TTL_MS = int(os.environ.get("REPLY_TTL_MS", str(300_000)))
# Expired jobs answer 410 for this long, then cleanup deletes JSON + WAVs (404 after).
JOB_RETAIN_MS = int(os.environ.get("JOB_RETAIN_MS", str(3_600_000)))
CLEANUP_INTERVAL_S = int(os.environ.get("CLEANUP_INTERVAL_S", "60"))
MAX_REPLY_BYTES = int(os.environ.get("MAX_REPLY_BYTES", str(4 * 1024 * 1024)))  # 4 MiB
MAX_UPLOAD_BYTES = int(os.environ.get("MAX_UPLOAD_BYTES", str(5 * 1024 * 1024)))

AUDIO_DIR = DATA_DIR / "audio"
REPLIES_DIR = DATA_DIR / "replies"
JOBS_DIR = DATA_DIR / "jobs"
STATUS_DIR = DATA_DIR / "status"
WAKE_FAIL_LOG = DATA_DIR / "wake_fail.log"

# Editable roster / placeholder voices (re-read on mtime change; no restart needed)
BOTS_PATH = Path(os.environ.get("BOTS_FILE", str(RELAY_DIR / "bots.json"))).resolve()
VOICES_PATH = Path(os.environ.get("VOICES_FILE", str(RELAY_DIR / "voices.json"))).resolve()
# Optional/TODO preview clips, one <voice_id>.wav per voice (GET /voices/<id>/sample.wav),
# to be generated with xAI grok-tts. Empty folder = Preview has nothing to play.
SAMPLES_DIR = Path(os.environ.get("VOICE_SAMPLES_DIR", str(DATA_DIR / "voice_samples"))).resolve()
# Non-idle status older than this is reported with stale=true (device shows idle)
STATUS_STALE_MS = int(os.environ.get("STATUS_STALE_MS", str(10 * 60_000)))

STATUS_STATES = ("idle", "listening", "thinking", "working", "speaking", "error")
STATUS_TEXT_MAX = 120
_ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")
_VOICE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}$")
_HEX_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")
# Avatar primitives the firmware can draw (main/include/bot_types.h). These mirror the
# app's avatarShape values; "circle" is the neutral default for bots without one.
BOT_SHAPES = ("blob", "teardrop", "cloud", "hex", "squircle", "circle")
BOT_SHAPE_ALIASES = {"hexagon": "hex", "drop": "teardrop", "round": "circle", "default": "circle"}
DEFAULT_PALETTE = {"default": {"color": "#64748B", "accent": "#CBD5E1"}}


def _ensure_dirs() -> None:
    for d in (DATA_DIR, AUDIO_DIR, REPLIES_DIR, JOBS_DIR, STATUS_DIR):
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


def now_ms() -> int:
    return int(time.time() * 1000)


def job_expired(job: dict) -> bool:
    exp = job.get("expires_at_ms")
    return isinstance(exp, (int, float)) and now_ms() > exp


def _expired_response():
    return jsonify(status="error", error="expired", message="job expired"), 410


def cleanup_expired_jobs() -> int:
    """Delete job JSON + upload/reply WAVs once a job is JOB_RETAIN_MS past expiry.

    Also removes orphan WAVs (no job JSON) older than JOB_TTL_MS + JOB_RETAIN_MS.
    Returns the number of jobs removed.
    """
    cutoff = now_ms() - JOB_RETAIN_MS
    removed = 0
    for path in JOBS_DIR.glob("*.json"):
        try:
            job = json.loads(path.read_text(encoding="utf-8"))
            exp = job.get("expires_at_ms") or (job.get("created_at_ms", 0) + JOB_TTL_MS)
        except (OSError, ValueError):
            exp = int(path.stat().st_mtime * 1000) + JOB_TTL_MS
        if exp >= cutoff:
            continue
        jid = path.stem
        for f in (path, AUDIO_DIR / f"{jid}.wav", REPLIES_DIR / f"{jid}.wav"):
            try:
                f.unlink(missing_ok=True)
            except OSError as e:
                log.warning("cleanup: could not delete %s: %s", f, e)
        removed += 1
    orphan_cutoff = time.time() - (JOB_TTL_MS + JOB_RETAIN_MS) / 1000
    for d in (AUDIO_DIR, REPLIES_DIR):
        for f in d.glob("*.wav"):
            try:
                if not (JOBS_DIR / f"{f.stem}.json").exists() and f.stat().st_mtime < orphan_cutoff:
                    f.unlink()
            except OSError:
                pass
    for f in JOBS_DIR.glob("*.json.tmp"):
        try:
            if f.stat().st_mtime < orphan_cutoff:
                f.unlink()
        except OSError:
            pass
    if removed:
        log.info("cleanup: removed %d expired job(s)", removed)
    return removed


def _cleanup_loop() -> None:
    while True:
        try:
            cleanup_expired_jobs()
        except Exception as e:  # never let the janitor die
            log.error("cleanup failed: %s", e)
        time.sleep(max(10, CLEANUP_INTERVAL_S))


# ---------------------------------------------------------------------------
# Text + WAV validation
# ---------------------------------------------------------------------------

_ASCII_MAP = str.maketrans({
    "\u2018": "'", "\u2019": "'", "\u201a": "'", "\u201b": "'",
    "\u201c": '"', "\u201d": '"', "\u201e": '"', "\u2032": "'", "\u2033": '"',
    "\u2013": "-", "\u2014": "-", "\u2015": "-", "\u2212": "-", "\u2010": "-", "\u2011": "-",
    "\u2026": "...", "\u00a0": " ", "\u2022": "-", "\u00b7": "-", "\u2192": "->",
})


def ascii_text(text: str, limit: int = 120) -> str:
    """Device fonts are ASCII only: map punctuation, strip accents, drop the rest, cap length."""
    t = str(text).translate(_ASCII_MAP)
    t = unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode("ascii")
    t = "".join(c if c.isprintable() else " " for c in t)
    t = " ".join(t.split())
    if len(t) > limit:
        t = t[: limit - 3].rstrip() + "..."
    return t


def wav_problem(raw: bytes) -> str | None:
    """None if raw is a RIFF/WAVE PCM file with a data chunk, else a short reason."""
    if len(raw) < 44 or raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        return "not a RIFF/WAVE file"
    pos, fmt_ok, have_data = 12, None, False
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            if len(body) < 16:
                return "fmt chunk too short"
            fmt_tag, channels, rate, _br, _ba, bits = struct.unpack("<HHIIHH", body[:16])
            if fmt_tag == 0xFFFE and len(body) >= 26:   # WAVE_FORMAT_EXTENSIBLE
                fmt_tag = struct.unpack("<H", body[24:26])[0]
            if fmt_tag != 1:
                return f"not PCM (format {fmt_tag})"
            if not (1 <= channels <= 2) or not (8000 <= rate <= 48000) or bits not in (8, 16, 24, 32):
                return f"unsupported PCM ({channels} ch, {rate} Hz, {bits} bit)"
            fmt_ok = True
        elif cid == b"data":
            have_data = True
            if fmt_ok:
                break
        pos += 8 + size + (size & 1)
    if not fmt_ok:
        return "missing fmt chunk"
    if not have_data:
        return "missing data chunk"
    return None


# ---------------------------------------------------------------------------
# Bot roster + placeholder voices (JSON files, cached by mtime)
# ---------------------------------------------------------------------------

_catalog_lock = threading.Lock()
_catalog_cache: dict[str, tuple[float, object]] = {}


def _load_json_cached(path: Path) -> object:
    """Load JSON, re-reading only when the file mtime changes."""
    key = str(path)
    try:
        mtime = path.stat().st_mtime
    except OSError:
        return None
    with _catalog_lock:
        hit = _catalog_cache.get(key)
        if hit and hit[0] == mtime:
            return hit[1]
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            log.error("could not parse %s: %s", path.name, e)
            return hit[1] if hit else None
        _catalog_cache[key] = (mtime, data)
        return data


def load_voices() -> list[dict]:
    data = _load_json_cached(VOICES_PATH) or {}
    out: list[dict] = []
    for v in data.get("voices", []) if isinstance(data, dict) else []:
        vid = str(v.get("id", "")).strip()
        if not _VOICE_RE.match(vid):
            continue
        out.append(
            {
                "id": vid,
                "name": str(v.get("name") or vid)[:40],
                "description": str(v.get("description") or "")[:160],
            }
        )
    return out


def voices_are_placeholder() -> bool:
    data = _load_json_cached(VOICES_PATH) or {}
    return bool(isinstance(data, dict) and data.get("placeholder", False))


def _resolve_color(value, palette: dict, field: str, fallback: str | None) -> str | None:
    """Palette name or literal #RRGGBB -> #RRGGBB (None/fallback when unknown)."""
    if value is None:
        return fallback
    v = str(value).strip()
    if _HEX_RE.match(v):
        return v.upper()
    entry = palette.get(v.lower())
    if isinstance(entry, dict):
        hexv = str(entry.get(field) or "").strip()
        if _HEX_RE.match(hexv):
            return hexv.upper()
    return fallback


def _int_in(value, lo: int, hi: int, default: int) -> int:
    try:
        return max(lo, min(hi, int(value)))
    except (TypeError, ValueError):
        return default


_warned_voices: set[tuple[str, str]] = set()


def load_bots() -> tuple[list[dict], str]:
    """Return (bots, default_bot_id). Invalid rows are skipped, not fatal.

    bots.json v2 nests the look under "avatar" with palette color names; the API flattens
    it to concrete #RRGGBB so the device never needs the palette.
    """
    data = _load_json_cached(BOTS_PATH) or {}
    if not isinstance(data, dict):
        data = {}
    palette = {k.lower(): v for k, v in (data.get("palette") or {}).items() if isinstance(v, dict)}
    for k, v in DEFAULT_PALETTE.items():
        palette.setdefault(k, v)
    voice_ids = {v["id"] for v in load_voices()}
    bots: list[dict] = []
    seen: set[str] = set()
    for b in data.get("bots", []):
        if not isinstance(b, dict):
            continue
        bid = str(b.get("id", "")).strip()
        if not _ID_RE.match(bid) or bid in seen:
            log.warning("bots.json: skipping invalid/duplicate id %r", bid)
            continue
        seen.add(bid)
        av = b.get("avatar") if isinstance(b.get("avatar"), dict) else b   # v1 rows were flat
        shape = str(av.get("shape") or "circle").strip().lower()
        shape = BOT_SHAPE_ALIASES.get(shape, shape)
        if shape not in BOT_SHAPES:
            log.warning("bots.json: %s has unknown shape %r -> circle", bid, shape)
            shape = "circle"
        color_name = str(av.get("color") or "default").strip()
        color = _resolve_color(color_name, palette, "color", None)
        if color is None:
            log.warning("bots.json: %s has unknown color %r -> default", bid, color_name)
            color_name = "default"
            color = _resolve_color("default", palette, "color", "#64748B")
        accent = (_resolve_color(av.get("accent"), palette, "color", None)
                  or _resolve_color(color_name, palette, "accent", "#FFFFFF"))
        rim = (_resolve_color(av.get("rim"), palette, "color", None)
               or _resolve_color(color_name, palette, "rim", None))
        row = {
            "id": bid,
            "name": str(b.get("name") or bid)[:24],
            "shape": shape,
            "color": color,
            "accent": accent,
            "color_name": color_name if not _HEX_RE.match(color_name) else "custom",
            "shape_scale": _int_in(av.get("scale"), 50, 100, 86),
            "shape_rotation": _int_in(av.get("rotation"), -180, 180, 0),
            "shape_wobble": _int_in(av.get("wobble"), 0, 100, 50),
            "shape_seed": _int_in(av.get("seed"), 0, 255, sum(map(ord, bid)) % 256),
            "avatar_tbd": bool(av.get("tbd", False)),
        }
        if rim:
            row["rim"] = rim
        dv = str(b.get("default_voice_id") or "").strip()
        if dv:
            if dv not in voice_ids and (bid, dv) not in _warned_voices:
                _warned_voices.add((bid, dv))
                log.warning("bots.json: %s default_voice_id %r is not in voices.json "
                            "(kept; the reply side decides what it sounds like)", bid, dv)
            if _VOICE_RE.match(dv):
                row["default_voice_id"] = dv
        bots.append(row)
    default_id = str(data.get("default_bot_id", ""))
    if bots and default_id not in seen:
        default_id = bots[0]["id"]
    return bots, default_id


def find_bot(bot_id: str) -> dict | None:
    bots, _ = load_bots()
    return next((b for b in bots if b["id"] == bot_id), None)


# ---------------------------------------------------------------------------
# Per-bot live status (persisted one JSON per bot)
# ---------------------------------------------------------------------------

def _status_path(bot_id: str) -> Path:
    if not _ID_RE.match(bot_id):
        raise ValueError("invalid bot_id")
    return STATUS_DIR / f"{bot_id}.json"


def write_status(bot_id: str, state: str, text: str, **extra) -> dict:
    rec = {
        "bot_id": bot_id,
        "state": state,
        "text": ascii_text(text, STATUS_TEXT_MAX),
        "updated_at_ms": int(time.time() * 1000),
    }
    for k, v in extra.items():
        if v is not None:
            rec[k] = v
    path = _status_path(bot_id)
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(rec), encoding="utf-8")
    tmp.replace(path)
    return rec


def read_status(bot_id: str) -> dict:
    path = _status_path(bot_id)
    rec = None
    if path.exists():
        try:
            rec = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            rec = None
    if not rec:
        return {"bot_id": bot_id, "state": "idle", "text": "Ready",
                "updated_at_ms": 0, "stale": False}
    age = int(time.time() * 1000) - int(rec.get("updated_at_ms", 0))
    rec["stale"] = bool(rec.get("state") != "idle" and age > STATUS_STALE_MS)
    return rec


def _clean_header_id(name: str, query_key: str, pattern: re.Pattern) -> tuple[str, bool]:
    """Read an id from header or query string. Returns (value, ok)."""
    raw = (request.headers.get(name) or request.args.get(query_key) or "").strip()
    if not raw:
        return "", True
    return raw, bool(pattern.match(raw))


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


def require_device_or_internal_auth() -> tuple | None:
    """Read endpoints (/bots, /voices, /status): device token or internal token."""
    token = _bearer_token()
    if INTERNAL_TOKEN and token and hmac.compare_digest(token, INTERNAL_TOKEN):
        return None
    return require_device_auth()


# ---------------------------------------------------------------------------
# Meridian wake
# ---------------------------------------------------------------------------

_env_file_cache: tuple[float, dict] | None = None


def _env_value(name: str) -> str:
    """Current value from relay/.env (re-read on change, so set-bot-webhook.sh needs no
    restart), falling back to the process environment."""
    global _env_file_cache
    try:
        mtime = ENV_PATH.stat().st_mtime
        if _env_file_cache is None or _env_file_cache[0] != mtime:
            _env_file_cache = (mtime, {k: (v or "") for k, v in dotenv_values(ENV_PATH).items()})
        val = _env_file_cache[1].get(name)
    except OSError:
        val = None
    if val is None:
        val = os.environ.get(name, "")
    return str(val).strip()


def bot_wake_target(bot_id: str) -> tuple[str, str, str]:
    """(route, url, key) for a voice turn aimed at bot_id.

    route = "direct"            the bot's own webhook (WAKE_URL_<BOT>/WAKE_KEY_<BOT>;
                                Meridian's own entry is MERIDIAN_WEBHOOK_URL/_SENDER_KEY)
            "meridian_fallback" no direct webhook: Meridian routes by target_bot_id
            "none"              nothing configured (dev mode, wake skipped)
    """
    if bot_id == "meridian":
        url, key = _env_value("MERIDIAN_WEBHOOK_URL"), _env_value("MERIDIAN_SENDER_KEY")
        return ("direct", url, key) if url and key else ("none", "", "")
    suffix = re.sub(r"[^A-Z0-9]", "_", bot_id.upper())
    url, key = _env_value(f"WAKE_URL_{suffix}"), _env_value(f"WAKE_KEY_{suffix}")
    if url and key:
        return "direct", url, key
    murl, mkey = _env_value("MERIDIAN_WEBHOOK_URL"), _env_value("MERIDIAN_SENDER_KEY")
    if murl and mkey:
        return "meridian_fallback", murl, mkey
    return "none", "", ""


def send_wake(url: str, key: str, body: dict, bot_id: str, route: str) -> bool:
    """POST the wake JSON once (8 s). On failure append to wake_fail.log. True if sent."""
    if not url or not key:
        log.warning("no webhook configured for %s (dev mode) - skip wake", bot_id)
        return False
    headers = {
        "Authorization": f"Bearer {key}",
        "X-Automation-Key": key,
        "Content-Type": "application/json",
    }
    try:
        resp = requests.post(url, json=body, headers=headers, timeout=8)
        if resp.status_code >= 400:
            raise RuntimeError(f"HTTP {resp.status_code}: {resp.text[:200]}")
        log.info("wake ok action=%s job_id=%s bot=%s route=%s status=%s",
                 body.get("action"), body.get("job_id"), bot_id, route, resp.status_code)
        return True
    except Exception as e:
        # requests error strings can contain the URL but never headers (no key leak)
        log.error("wake failed action=%s job_id=%s bot=%s route=%s: %s",
                  body.get("action"), body.get("job_id"), bot_id, route, e)
        _append_wake_fail(body, str(e), bot_id=bot_id, route=route)
        return False


def wake_meridian(body: dict) -> bool:
    """Back-compat helper: wake Meridian's own webhook."""
    _route, url, key = bot_wake_target("meridian")
    return send_wake(url, key, body, "meridian", "direct")


def _append_wake_fail(body: dict, error: str, bot_id: str | None = None,
                      route: str | None = None) -> None:
    record = {
        "failed_at_ms": int(time.time() * 1000),
        "bot_id": bot_id,
        "route": route,
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

    # Multi-bot routing (headers preferred; query string accepted for curl tests)
    target_bot_id, ok = _clean_header_id("X-Target-Bot-Id", "target_bot_id", _ID_RE)
    if not ok:
        return jsonify(error="invalid target_bot_id"), 400
    bots, default_bot_id = load_bots()
    if not target_bot_id:
        target_bot_id = default_bot_id or "meridian"
    bot = next((b for b in bots if b["id"] == target_bot_id), None)
    if bot is None:   # fail closed, including when bots.json is missing/empty/broken
        if not bots:
            log.error("upload rejected: bot roster is empty (check %s)", BOTS_PATH)
        return jsonify(error="unknown target_bot_id", target_bot_id=target_bot_id), 400

    voice_id, ok = _clean_header_id("X-Voice-Id", "voice_id", _VOICE_RE)
    if not ok:
        return jsonify(error="invalid voice_id"), 400
    if not voice_id and bot:
        voice_id = bot.get("default_voice_id", "")
    if voice_id and voice_id not in {v["id"] for v in load_voices()}:
        # Pass through anyway: the reply side owns the real voice catalog.
        log.info("voice_id %s not in voices.json — forwarding as-is", voice_id)

    conversation_id, ok = _clean_header_id(
        "X-Conversation-Id", "conversation_id", _VOICE_RE
    )
    if not ok:
        return jsonify(error="invalid conversation_id"), 400

    raw = request.get_data()
    if not raw:
        return jsonify(error="empty body"), 400
    if len(raw) > MAX_UPLOAD_BYTES:
        return jsonify(error="upload too large"), 413

    job_id = new_job_id()
    created_ms = now_ms()
    expires_at_ms = created_ms + JOB_TTL_MS

    audio_path = AUDIO_DIR / f"{job_id}.wav"
    audio_path.write_bytes(raw)

    audio_url = f"{PUBLIC_BASE_URL}/audio/{job_id}.wav"
    result_url = f"{PUBLIC_BASE_URL}/result/{job_id}"

    wake_route, wake_url, wake_key = bot_wake_target(target_bot_id)
    job = {
        "job_id": job_id,
        "device_id": device_id,
        "status": "pending",
        "created_at_ms": created_ms,
        "expires_at_ms": expires_at_ms,
        "audio_url": audio_url,
        "result_url": result_url,
        "audio_path": str(audio_path),
        "reply_audio_url": None,
        "error": None,
        "target_bot_id": target_bot_id,
        "voice_id": voice_id or None,
        "conversation_id": conversation_id or None,
        "wake_route": wake_route,
    }
    save_job(job)

    status_url = f"{PUBLIC_BASE_URL}/status/{target_bot_id}"
    wake_body = {
        "action": "voice_turn",
        "job_id": job_id,
        "device_id": device_id,
        "audio_url": audio_url,
        "result_url": result_url,
        "timestamp_ms": created_ms,
        "target_bot_id": target_bot_id,
        "target_bot_name": bot["name"] if bot else target_bot_id,
        "voice_id": voice_id or None,
        "conversation_id": conversation_id or None,
        "status_url": status_url,
    }
    try:
        write_status(
            target_bot_id, "thinking",
            f"Sent to {wake_body['target_bot_name']}" if wake_route == "direct"
            else f"Meridian is passing this to {wake_body['target_bot_name']}",
            job_id=job_id, source="relay",
        )
    except (OSError, ValueError) as e:
        log.warning("status write failed for %s: %s", target_bot_id, e)
    wake_body["wake_route"] = wake_route
    woken = send_wake(wake_url, wake_key, wake_body, target_bot_id, wake_route)
    # Always 201 with pending result even if wake failed

    return (
        jsonify(
            job_id=job_id,
            audio_url=audio_url,
            result_url=result_url,
            status_url=status_url,
            expires_at_ms=expires_at_ms,
            target_bot_id=target_bot_id,
            voice_id=voice_id or None,
            wake_route=wake_route,
            woken=woken,
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
    if job_expired(job):
        return _expired_response()

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
            "conversation_id",
            "wake_route",
        ):
            payload[k] = v
    return jsonify(payload), 200


def _job_wav(job_id: str, folder: Path):
    """Shared 404/410 handling for job WAVs. Returns (path, None) or (None, response)."""
    try:
        _job_path(job_id)
    except ValueError:
        return None, (jsonify(error="not found"), 404)
    job = load_job(job_id)
    if job and job_expired(job):
        return None, _expired_response()
    path = folder / f"{job_id}.wav"
    if not job or not path.exists():
        return None, (jsonify(error="not found"), 404)
    return path, None


@app.get("/audio/<job_id>.wav")
def get_audio(job_id: str):
    """The device's upload. Only the reply side reads it: internal bearer required."""
    err = require_internal_auth()
    if err:
        return err
    path, err = _job_wav(job_id, AUDIO_DIR)
    if err:
        return err
    return send_file(path, mimetype="audio/wav", as_attachment=False, max_age=0)


@app.get("/replies/<job_id>.wav")
def get_reply(job_id: str):
    """The spoken reply. The device downloads it (device token); internal also allowed."""
    err = require_device_or_internal_auth()
    if err:
        return err
    path, err = _job_wav(job_id, REPLIES_DIR)
    if err:
        return err
    return send_file(path, mimetype="audio/wav", as_attachment=False, max_age=0)


@app.put("/internal/result/<job_id>")
def put_result(job_id: str):
    err = require_internal_auth()
    if err:
        return err
    if request.content_length is not None and request.content_length > MAX_REPLY_BYTES:
        return jsonify(error="reply too large", max_bytes=MAX_REPLY_BYTES), 413

    job = load_job(job_id)
    if not job:
        return jsonify(error="unknown job"), 404
    if job_expired(job):
        return _expired_response()

    ctype = (request.content_type or "").lower()
    raw = request.get_data()
    if len(raw) > MAX_REPLY_BYTES:
        return jsonify(error="reply too large", max_bytes=MAX_REPLY_BYTES), 413
    if "audio" in ctype or (raw and not ctype.startswith("application/json")):
        # Raw WAV reply (audio/wav preferred; any non-JSON body is treated as audio)
        if not raw:
            return jsonify(error="empty audio body"), 400
        problem = wav_problem(raw)
        if problem:
            return jsonify(error="invalid wav", detail=problem), 400
        reply_path = REPLIES_DIR / f"{job_id}.wav"
        reply_path.write_bytes(raw)
        job["status"] = "ready"
        job["reply_audio_url"] = f"{PUBLIC_BASE_URL}/replies/{job_id}.wav"
        job["reply_path"] = str(reply_path)
        job["error"] = None
        _extend_for_download(job)
        save_job(job)
        _status_after_result(job)
        return jsonify(ok=True, job_id=job_id, status="ready"), 200

    data = request.get_json(force=True, silent=True)
    if not isinstance(data, dict):
        return jsonify(error="JSON object or audio/wav body required"), 400
    status = str(data.get("status") or "").strip().lower()
    # Only "status" decides the outcome; message/error keys are just details.
    if status == "error":
        job["status"] = "error"
        job["error"] = ascii_text(str(data.get("message") or data.get("error") or "error"), 200)
        save_job(job)
        _status_after_result(job)
        return jsonify(ok=True, job_id=job_id, status="error"), 200
    if status != "ready":
        return jsonify(error="status must be 'ready' or 'error'"), 400

    if "reply_audio_url" in data:
        job["reply_audio_url"] = data["reply_audio_url"]
    for k, v in data.items():
        if k in ("status", "job_id", "error", "message", "expires_at_ms", "created_at_ms",
                 "audio_path", "reply_path", "audio_url", "result_url", "device_id"):
            continue
        if k not in job or k in ("reply_audio_url", "transcript", "text"):
            job[k] = v
    job["status"] = "ready"
    job["error"] = None
    if not job.get("reply_audio_url"):
        reply_path = REPLIES_DIR / f"{job_id}.wav"
        if reply_path.exists():
            job["reply_audio_url"] = f"{PUBLIC_BASE_URL}/replies/{job_id}.wav"
            job["reply_path"] = str(reply_path)
    _extend_for_download(job)
    save_job(job)
    _status_after_result(job)
    return jsonify(ok=True, job_id=job_id, status="ready"), 200


def _extend_for_download(job: dict) -> None:
    """Give the device REPLY_TTL_MS to fetch a reply that landed near the deadline."""
    job["expires_at_ms"] = max(int(job.get("expires_at_ms") or 0), now_ms() + REPLY_TTL_MS)


def _status_after_result(job: dict) -> None:
    """When a reply lands, settle the bot's status unless Meridian already did."""
    bot_id = job.get("target_bot_id")
    if not bot_id:
        return
    try:
        cur = read_status(bot_id)
        if cur.get("job_id") not in (None, job["job_id"]):
            return  # a newer turn owns the status line
        if job.get("status") == "error":
            write_status(bot_id, "error", str(job.get("error") or "Something went wrong")[:STATUS_TEXT_MAX],
                         job_id=job["job_id"], source="relay")
        elif job.get("status") == "ready":
            write_status(bot_id, "idle", "Reply ready", job_id=job["job_id"], source="relay")
    except (OSError, ValueError) as e:
        log.warning("status settle failed for %s: %s", bot_id, e)


@app.get("/bots")
def get_bots():
    err = require_device_or_internal_auth()
    if err:
        return err
    bots, default_id = load_bots()
    return jsonify(version=1, default_bot_id=default_id, bots=bots), 200


@app.get("/voices")
def get_voices():
    err = require_device_or_internal_auth()
    if err:
        return err
    voices = load_voices()
    for v in voices:
        if (SAMPLES_DIR / f"{v['id']}.wav").is_file():
            v["sample_path"] = f"/voices/{v['id']}/sample.wav"
    return jsonify(version=1, placeholder=voices_are_placeholder(), voices=voices), 200


@app.get("/voices/<voice_id>/sample.wav")
def get_voice_sample(voice_id: str):
    """Short preview clip for the device's voice picker (16 kHz mono PCM WAV)."""
    err = require_device_or_internal_auth()
    if err:
        return err
    if not _VOICE_RE.match(voice_id) or voice_id not in {v["id"] for v in load_voices()}:
        return jsonify(error="unknown voice"), 404
    path = SAMPLES_DIR / f"{voice_id}.wav"
    if not path.is_file():
        return jsonify(error="no sample for this voice"), 404
    return send_file(path, mimetype="audio/wav", as_attachment=False, max_age=3600)


@app.get("/status/<bot_id>")
def get_status(bot_id: str):
    err = require_device_or_internal_auth()
    if err:
        return err
    if not _ID_RE.match(bot_id) or not find_bot(bot_id):
        return jsonify(error="unknown bot"), 404
    return jsonify(read_status(bot_id)), 200


@app.put("/internal/status/<bot_id>")
def put_status(bot_id: str):
    """Meridian pushes live status: {state, text, job_id?}."""
    err = require_internal_auth()
    if err:
        return err
    if not _ID_RE.match(bot_id) or not find_bot(bot_id):
        return jsonify(error="unknown bot"), 404
    data = request.get_json(force=True, silent=True)
    if not isinstance(data, dict):
        return jsonify(error="JSON body required"), 400
    state = str(data.get("state", "")).strip().lower()
    if state not in STATUS_STATES:
        return jsonify(error="invalid state", allowed=list(STATUS_STATES)), 400
    text = ascii_text(str(data.get("text") or ""), STATUS_TEXT_MAX)
    if not text:
        text = {"idle": "Ready", "listening": "Listening", "thinking": "Thinking...",
                "working": "Working on it", "speaking": "Speaking",
                "error": "Something went wrong"}[state]
    job_id = data.get("job_id")
    if job_id is not None and not (isinstance(job_id, str) and _VOICE_RE.match(job_id)):
        return jsonify(error="invalid job_id"), 400
    rec = write_status(bot_id, state, text, job_id=job_id, source="meridian")
    return jsonify(ok=True, **rec), 200


@app.post("/probe")
def probe():
    """Health-check a wake path with action=probe. ?bot=<id> probes the route a voice turn
    for that bot would take (its direct webhook, else Meridian). Default: Meridian."""
    err = require_device_or_internal_auth()
    if err:
        return err
    bot_id = (request.args.get("bot") or "meridian").strip().lower()
    if not _ID_RE.match(bot_id) or (bot_id != "meridian" and not find_bot(bot_id)):
        return jsonify(error="unknown bot"), 404
    route, url, key = bot_wake_target(bot_id)
    device_id = request.headers.get("X-Device-Id", "probe").strip() or "probe"
    job_id = "probe_" + uuid.uuid4().hex[:8]
    body = {
        "action": "probe",
        "job_id": job_id,
        "device_id": device_id,
        "timestamp_ms": now_ms(),
        "target_bot_id": bot_id,
        "wake_route": route,
    }
    ok = send_wake(url, key, body, bot_id, route) if route != "none" else False
    return jsonify(ok=ok, job_id=job_id, bot=bot_id, wake_route=route, woken=ok), 200 if ok else 503


@app.get("/internal/wake-config")
def wake_config():
    """Which bots wake directly vs through Meridian. Never returns URLs' paths or keys."""
    err = require_internal_auth()
    if err:
        return err
    bots, _ = load_bots()
    ids = [b["id"] for b in bots] or ["meridian"]
    if "meridian" not in ids:
        ids.insert(0, "meridian")
    out = []
    for bid in ids:
        route, url, _key = bot_wake_target(bid)
        host = requests.utils.urlparse(url).hostname if url else None
        out.append({"bot_id": bid, "route": route, "webhook_host": host})
    m_route, _, _ = bot_wake_target("meridian")
    return jsonify(meridian_configured=(m_route == "direct"), bots=out), 200


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
    threading.Thread(target=_cleanup_loop, name="job-cleanup", daemon=True).start()
    app.run(host=RELAY_BIND, port=RELAY_PORT, threaded=True)


if __name__ == "__main__":
    main()

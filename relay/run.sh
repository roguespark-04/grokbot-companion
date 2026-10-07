#!/usr/bin/env bash
# Start Grok Bot Companion audio relay (loads relay/.env).
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  echo "No .env found — copying from .env.example"
  cp .env.example .env
  echo "Edit relay/.env (especially PUBLIC_BASE_URL, MERIDIAN_*) then re-run."
fi

# shellcheck disable=SC1091
set -a
# dotenv will also load inside server.py; exporting here helps any child tools
source .env
set +a

if [[ ! -d .venv ]]; then
  python3 -m venv .venv
  .venv/bin/pip install -q -r requirements.txt
fi

# Ensure deps present (idempotent)
.venv/bin/pip install -q -r requirements.txt

exec .venv/bin/python server.py

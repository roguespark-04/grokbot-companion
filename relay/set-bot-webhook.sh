#!/usr/bin/env bash
# Configure a bot's DIRECT wake webhook in relay/.env without ever printing the key.
#
#   usage: ./set-bot-webhook.sh <bot_id> <webhook_url> <ENV_VAR_WITH_KEY>
#   e.g.:  PHOTON_KEY=... ./set-bot-webhook.sh photon https://example/hooks/abc PHOTON_KEY
#
# Writes WAKE_URL_<BOT>=... and WAKE_KEY_<BOT>=... (bot id upper-cased, non-alnum -> _).
# For bot "meridian" it writes MERIDIAN_WEBHOOK_URL / MERIDIAN_SENDER_KEY instead.
# The key is read from the named environment variable, never from argv (keeps it out of
# shell history and `ps`). The relay re-reads .env on each wake, so no restart is needed;
# the script confirms via GET /internal/wake-config (route only, never keys).
set -euo pipefail
cd "$(dirname "$0")"

if [[ $# -ne 3 ]]; then
  sed -n '4,6p' "$0" >&2
  exit 2
fi
bot_id=$1 url=$2 key_var=$3

[[ $bot_id =~ ^[a-z0-9][a-z0-9_-]{0,31}$ ]] || { echo "error: bad bot_id" >&2; exit 2; }
[[ $url =~ ^https?://[^[:space:]\']+$ ]] || { echo "error: url must be http(s):// with no spaces or quotes" >&2; exit 2; }
[[ $key_var =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { echo "error: third argument is the NAME of an env var" >&2; exit 2; }
key=${!key_var-}
[[ -n $key ]] || { echo "error: env var $key_var is empty or unset" >&2; exit 2; }
if [[ $key == *"'"* || $key == *$'\n'* ]]; then
  echo "error: key contains a quote or newline; not supported" >&2; exit 2
fi

suffix=$(printf '%s' "$bot_id" | tr '[:lower:]' '[:upper:]' | tr -c 'A-Z0-9\n' '_')
if [[ $bot_id == meridian ]]; then
  url_name=MERIDIAN_WEBHOOK_URL key_name=MERIDIAN_SENDER_KEY
else
  url_name=WAKE_URL_$suffix key_name=WAKE_KEY_$suffix
fi

touch .env
chmod 600 .env
tmp=$(mktemp .env.XXXXXX)
chmod 600 "$tmp"
# Drop old lines for these two names, then append the new ones (single-quoted so both
# python-dotenv and `source .env` in run.sh read them verbatim).
grep -v -E "^(${url_name}|${key_name})=" .env > "$tmp" || true
printf "%s='%s'\n" "$url_name" "$url" >> "$tmp"
KEYVAL=$key KEYNAME=$key_name python3 -c 'import os,sys; sys.stdout.write("%s=\x27%s\x27\n" % (os.environ["KEYNAME"], os.environ["KEYVAL"]))' >> "$tmp"
mv "$tmp" .env
echo "wrote $url_name and $key_name to relay/.env (key not shown)"

# Confirm the running relay sees it (internal token read from .env, never echoed).
itok=$(grep -E '^INTERNAL_TOKEN=' .env | tail -1 | cut -d= -f2- | sed -e "s/^'//" -e "s/'$//" -e 's/^"//' -e 's/"$//')
port=$(grep -E '^RELAY_PORT=' .env | tail -1 | cut -d= -f2- | tr -d "'\"")
port=${port:-8787}
if [[ -n $itok ]] && out=$(curl -fsS -m 5 -H "Authorization: Bearer $itok" "http://127.0.0.1:${port}/internal/wake-config" 2>/dev/null); then
  BOT=$bot_id python3 -c 'import json,os,sys; d=json.load(sys.stdin); b=[x for x in d["bots"] if x["bot_id"]==os.environ["BOT"]]; print("relay sees %s -> %s" % (os.environ["BOT"], b[0]["route"] if b else "not in roster"))' <<<"$out"
else
  echo "note: relay not reachable on 127.0.0.1:${port}; it will pick this up on next start"
fi

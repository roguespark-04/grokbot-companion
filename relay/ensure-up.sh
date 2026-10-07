#!/usr/bin/env bash
# Bring the Grok Bot Companion relay back after a box restart.
# Starts tailscaled and the relay only if they aren't already running.
set -u
cd "$(dirname "$0")"
if ! pgrep -x tailscaled >/dev/null; then
  sudo mkdir -p /run/tailscale
  sudo nohup tailscaled --state=/var/lib/tailscale/tailscaled.state \
    --socket=/run/tailscale/tailscaled.sock >/tmp/tailscaled.log 2>&1 &
  sleep 3
fi
if ! curl -fsS -m 3 http://127.0.0.1:8787/health >/dev/null 2>&1; then
  nohup ./run.sh >> /tmp/grokbot-relay.log 2>&1 &
  sleep 3
fi
curl -fsS -m 3 http://127.0.0.1:8787/health && echo && tailscale ip -4

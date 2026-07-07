#!/usr/bin/env bash
# dev-mac.sh — one-command web-UI dev loop on macOS.
#
# Brings up BOTH halves of the dev setup and ties their lifetimes together:
#   1. the desktop simulator  — the real C++ backend, serving /api + the SSE
#      stream (and live board data) on http://localhost:8080
#   2. the Vite dev server    — the Svelte UI with hot-module reload on :5173,
#      already configured (webui/vite.config.js) to proxy /api -> :8080
#
# Then just open  http://localhost:5173  : you get the live board state from the
# simulator AND instant reload as you edit webui/. (Opening :8080 directly still
# serves the last *built* web/ — use :5173 for the hot-reloading version.)
#
# The browser talks only to Vite; Vite proxies the API to the sim server-side,
# so there's no CORS and no need to teach the C++ server about Vite.
#
# Usage:   scripts/dev-mac.sh [model.nam]
#   - optional first arg: a .nam capture to load (else the sim runs a clean amp)
#   - Ctrl-C in this terminal stops Vite and the simulator together.
#
# If something is already listening on :8080 (e.g. a sim you launched yourself),
# this script uses it and won't start a second one.

set -euo pipefail
cd "$(dirname "$0")/.."

# JUCE writes the macOS binary (+ its web/ rigs/ presets/ setlists/) under Release/.
SIM=build-mac/pedalboard_artefacts/Release/pedalboard
[[ -x "$SIM" ]] || SIM=build-mac/pedalboard_artefacts/pedalboard

SIM_PID=""
cleanup() { [[ -n "$SIM_PID" ]] && kill "$SIM_PID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

if nc -z localhost 8080 2>/dev/null; then
  echo "→ Backend already on :8080 — using it (not launching a new simulator)."
else
  if [[ ! -x "$SIM" ]]; then
    echo "→ Simulator not built — building it (scripts/build-mac.sh pedalboard)…"
    scripts/build-mac.sh pedalboard
  fi
  echo "→ Launching simulator (backend + API on :8080)…"
  if [[ $# -gt 0 ]]; then "$SIM" "$1" & else "$SIM" & fi
  SIM_PID=$!
fi

echo "→ Starting Vite dev server on :5173 (proxying /api -> :8080)…"
echo "  Open http://localhost:5173  ·  Ctrl-C here stops everything."
cd webui
[[ -d node_modules ]] || npm install
npm run dev

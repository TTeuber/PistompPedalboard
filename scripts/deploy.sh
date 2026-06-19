#!/usr/bin/env bash
# Copy a built binary to the Pi over SSH (and optionally run it).
#
#   scripts/deploy.sh rt_passthrough            # rsync to the Pi
#   scripts/deploy.sh rt_passthrough --run      # rsync, then run it over SSH
#   scripts/deploy.sh pistomp_app --run
#
# Pi connection (override via env if your setup differs):
#   PI_HOST=pistomp.local  PI_USER=patch  PI_DIR=~/app
# Uses your existing ~/.ssh keys. No runtime libs are shipped: the Pi already has
# libasound2/libgpiod2 (same bookworm release), so the binary just runs.
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:?usage: deploy.sh <binary-name> [--run [args...]]}"
shift

PI_HOST="${PI_HOST:-pistomp.local}"
PI_USER="${PI_USER:-pistomp}"
PI_DIR="${PI_DIR:-~/app}"

# Find the built binary anywhere under build/ (JUCE nests its output under
# *_artefacts/, the sandbox targets sit in build/sandbox/).
BIN="$(find build -type f -name "$TARGET" -perm +111 2>/dev/null | head -1 || true)"
if [[ -z "$BIN" ]]; then
  echo "error: no executable named '$TARGET' under build/. Build it first:" >&2
  echo "  scripts/build.sh $TARGET" >&2
  exit 1
fi

echo ">> deploying $BIN -> $PI_USER@$PI_HOST:$PI_DIR/"
ssh "$PI_USER@$PI_HOST" "mkdir -p $PI_DIR"
rsync -avz "$BIN" "$PI_USER@$PI_HOST:$PI_DIR/"

if [[ "${1:-}" == "--run" ]]; then
  shift
  echo ">> running $PI_DIR/$TARGET on the Pi"
  exec ssh -t "$PI_USER@$PI_HOST" "$PI_DIR/$TARGET $*"
fi

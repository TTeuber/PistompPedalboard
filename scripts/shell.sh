#!/usr/bin/env bash
# Open an interactive shell in the persistent dev container (repo at /work).
# Same container the build script uses, so you share its warm ccache.
set -euo pipefail
cd "$(dirname "$0")/.."
docker compose up -d dev >/dev/null
exec docker compose exec dev bash

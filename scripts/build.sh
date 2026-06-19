#!/usr/bin/env bash
# Configure (once) and build inside the persistent dev container.
#
#   scripts/build.sh                 # build everything
#   scripts/build.sh rt_passthrough  # build one target
#   scripts/build.sh pistomp_app
#
# Build artifacts land in ./build on the Mac (bind mount); ccache lives on a
# named volume. Both persist, so rebuilds are incremental and clean rebuilds
# reuse cached objects. After editing docker/Dockerfile, run `docker compose
# build` to refresh the image.
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:-}"

docker compose up -d dev >/dev/null
docker compose exec -T dev bash -lc "
  set -e
  cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build ${TARGET:+--target ${TARGET}}
"

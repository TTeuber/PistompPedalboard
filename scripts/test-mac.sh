#!/usr/bin/env bash
# test-mac.sh — build + run the pedalboard test suite natively on the Mac.
#
#   scripts/test-mac.sh          # plain Release build (build-mac/), full suite
#   scripts/test-mac.sh tsan     # ThreadSanitizer build (build-mac-tsan/):
#                                # races in the chain's RCU publish/retire path
#   scripts/test-mac.sh asan     # AddressSanitizer build (build-mac-asan/):
#                                # use-after-free past the epoch grace period
#
# Each sanitizer gets its OWN build dir: sanitized objects can't be mixed with
# plain ones (or each other), and this keeps the normal build-mac/ untouched.
# Only the pedalboard_tests target is built, so the sanitizer dirs skip the
# app, the web UI npm build, and the HAL.
#
# A TSan run that finds a race exits non-zero (TSan's default exitcode=66), so
# ctest reports the test as failed; ASan aborts on the first bad access.

set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-}"
case "$MODE" in
  "")    DIR=build-mac;      SAN="" ;;
  tsan)  DIR=build-mac-tsan; SAN=thread ;;
  asan)  DIR=build-mac-asan; SAN=address ;;
  *) echo "usage: $0 [tsan|asan]" >&2; exit 2 ;;
esac

# CMAKE_POLICY_VERSION_MINIMUM: same vendored-deps workaround as build-mac.sh.
cmake -G Ninja -B "$DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  ${SAN:+-DSANITIZE=$SAN}

cmake --build "$DIR" --target pedalboard_tests
ctest --test-dir "$DIR" --output-on-failure

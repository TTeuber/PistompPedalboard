#!/usr/bin/env bash
# build-mac.sh — native macOS build of the pedalboard, for developing without
# the Pi. Builds the desktop SIMULATOR: LVGL in an SDL window, audio via
# miniaudio/CoreAudio, controls from the keyboard. The Pi build is unaffected --
# that still goes through Docker (scripts/build.sh) and produces arm64 binaries.
#
# IMPORTANT: the simulator validates correctness, sound, and UX -- NOT the Pi's
# realtime CPU budget. The Mac is far faster than a Pi 5, so always validate DSP
# load and xruns on real hardware before shipping.
#
# Prereqs (one time):   brew install cmake ninja sdl2
# Build:                scripts/build-mac.sh            # everything
#                       scripts/build-mac.sh pedalboard # one target
# Run:                  ./build-mac/pedalboard_artefacts/pedalboard <model.nam>
#
# Keyboard controls in the sim window:
#   Nav encoder  Left / Right        Nav select (hold = quit)  Return
#   Enc1 Q / W   + button 1          Enc2 A / S  + button 2
#   Enc3 Z / X   (master level)      Footswitches FS0..3       F1 F2 F3 F4
#
# Uses a SEPARATE build dir (build-mac/) from the Pi build (build/): the two have
# different toolchains/ABIs and must not share FetchContent _deps.

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=build-mac

# CMAKE_POLICY_VERSION_MINIMUM lets the vendored deps (LVGL/JUCE/NAM) that still
# declare cmake_minimum_required(<3.5) configure under CMake 4.x.
cmake -G Ninja -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build "$BUILD_DIR" ${1:+--target "$1"}

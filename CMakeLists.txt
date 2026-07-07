# PistompPedalboard -- a live multi-effect guitar pedalboard for the pi-Stomp
# (Raspberry Pi 5), with an on-device LVGL UI and a Svelte web control surface.
#
# Dependencies (all FetchContent, pinned by tag):
#   * pistomp_hal -- the hardware layer (controls, display, realtime audio);
#                    on macOS it provides the desktop simulator backend
#   * JUCE        -- dsp/audio modules for the heavy effects
#   * nam_core    -- Neural Amp Modeler inference for the amp block
#   * cpp-httplib -- the web UI's HTTP/SSE server

cmake_minimum_required(VERSION 3.22)
project(PistompPedalboard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Emit compile_commands.json so clangd (and other tooling) sees the exact
# include paths / defines each file is built with. Symlinked into the repo root
# from build-mac/ for the native editing setup; gitignored.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Route compiles through ccache when present (it is, in the dev container) so
# clean rebuilds reuse cached objects instead of recompiling all of JUCE.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Optional Pi-specific tuning. OFF -> generic armv8-a, which is fine and still
# has NEON (armv8 mandates it). Turn ON (-DPI_TUNE=ON) to tune codegen for the
# Pi 5's Cortex-A76 once DSP load actually matters.
#
# Deliberately NOT -march=native: this compiles on an Apple core, not the Pi, so
# "native" would optimize for the wrong CPU (and could emit instructions the Pi
# lacks). -mcpu=cortex-a76 names the *target* explicitly.
option(PI_TUNE "Tune codegen for the Pi 5 Cortex-A76" OFF)
if(PI_TUNE)
  add_compile_options(-mcpu=cortex-a76)
endif()

# ---- sanitizers (-DSANITIZE=thread|address) ---------------------------------
# Instruments EVERYTHING built here (deps included -- set before any targets)
# with TSan or ASan. Use a separate build dir per sanitizer; they can't share
# objects with each other or with a plain build (scripts/test-mac.sh tsan|asan
# handles that). Main customer: the chain-edit stress test in tests/ -- TSan
# checks the RCU epoch-reclamation protocol's happens-before edges, ASan
# catches any use-after-free the grace period would miss.
set(SANITIZE "" CACHE STRING "Build with a sanitizer: '', 'thread', or 'address'")
if(SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread -g -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address -g -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
elseif(NOT SANITIZE STREQUAL "")
  message(FATAL_ERROR "SANITIZE must be '', 'thread', or 'address' (got '${SANITIZE}')")
endif()

include(FetchContent)

# ---- pistomp_hal: the hardware abstraction layer -----------------------------
# Pinned to a release tag; bump deliberately when the HAL API moves. To hack on
# the HAL and the pedalboard together, point the fetch at a local checkout:
#   cmake -B build-mac -DFETCHCONTENT_SOURCE_DIR_PISTOMP_HAL=/path/to/pistomp-hal
FetchContent_Declare(pistomp_hal
  GIT_REPOSITORY https://github.com/TTeuber/pistomp-hal.git
  GIT_TAG v0.1.0
  GIT_SHALLOW TRUE
)

# ---- Neural Amp Modeler core (pure-C++ NAM inference, no Python) ------------
# Vendored via FetchContent, pinned to v0.5.3 -- the first tag carrying the
# `SlimmableContainer` architecture our SuperReverbNAM/*.nam files use. We build
# its sources into our own static lib instead of using its CMake, which only
# builds NAM's command-line tools (and would drag in the AudioDSPTools submodule
# just for WAV I/O we don't need). NAM requires C++20; we scope that to this
# target so the rest of the repo stays C++17.
FetchContent_Declare(namcore
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG v0.5.3
  GIT_SHALLOW TRUE
  # eigen is the only submodule we need; nlohmann/json is vendored in-tree and
  # AudioDSPTools (the third submodule) is only used by NAM's own tools.
  GIT_SUBMODULES "Dependencies/eigen"
  # Point add_subdirectory() at a dir with no CMakeLists.txt so MakeAvailable
  # POPULATES the source but does NOT configure/build NAM's tools.
  SOURCE_SUBDIR Dependencies/nlohmann
)

# ---- JUCE (audio/DSP framework) ----------------------------------------------
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.13          # latest stable; bump deliberately
  GIT_SHALLOW TRUE
)

# ---- cpp-httplib: header-only HTTP server for the web UI ---------------------
# Plain HTTP only (no OpenSSL/zlib/brotli) -- keeps the Pi dependency-free.
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(pistomp_hal namcore JUCE httplib)

# NAM sources live in NAM/*.cpp plus the NAM/wavenet/ subdir.
file(GLOB NAM_CORE_SOURCES
  "${namcore_SOURCE_DIR}/NAM/*.cpp"
  "${namcore_SOURCE_DIR}/NAM/*/*.cpp")

# OBJECT (not STATIC) library on purpose: NAM registers architectures like
# `SlimmableContainer` via file-scope static initializers (container.cpp). In a
# STATIC archive the linker drops those objects (nothing references them) and the
# registration never runs -> "No config parser registered". An OBJECT library
# folds every object straight into each consumer, keeping the initializers --
# the same effect as NAM's tools compiling all sources into the executable.
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
  "${namcore_SOURCE_DIR}"                        # so consumers #include "NAM/dsp.h"
  "${namcore_SOURCE_DIR}/Dependencies/eigen"
  "${namcore_SOURCE_DIR}/Dependencies/nlohmann") # provides <json.hpp>
target_compile_features(nam_core PUBLIC cxx_std_20)
# Match NAM's default: route A2-shaped models to the hand-optimized WaveNet.
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
if(NOT MSVC)
  # Third-party DSP hot path: silence its warnings, optimize hard.
  target_compile_options(nam_core PRIVATE -w -O3)
endif()

find_package(Threads REQUIRED)
# ALSA only exists on the Pi; on the macOS sim the HAL uses miniaudio/CoreAudio
# instead, so there's nothing extra to link here.
if(NOT APPLE)
  find_library(ASOUND_LIB asound REQUIRED)
endif()

# --- the live pedal (Pi: ALSA audio + hardware + web) -----------------------
juce_add_console_app(pedalboard PRODUCT_NAME "pedalboard")
target_sources(pedalboard PRIVATE
  pedalboard.cpp
  fx_registry.cpp
  ui/ui_controller.cpp
  web_server.cpp
  rigs.cpp
  presets.cpp
  setlists.cpp
  meta.cpp
  manifest.cpp
)
target_compile_definitions(pedalboard PRIVATE
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_include_directories(pedalboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pedalboard PRIVATE
  pistomp_hal
  nam_core
  httplib::httplib
  juce::juce_core
  juce::juce_audio_basics
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
  ${ASOUND_LIB}   # empty on macOS (guarded above)
  Threads::Threads
)

# --- web UI: Svelte + Vite, built into web/ (served + rsynced as static files) ---
# The frontend lives in webui/ (a Vite project) and builds into web/, which the
# server mounts at / (resolved next to the binary) and deploy.sh rsyncs to the
# Pi. We hang it off the C++ build so `scripts/build.sh` produces everything in
# one shot. It's INCREMENTAL: the stamp's DEPENDS list is the webui sources, so
# npm only runs when the frontend actually changed (edits are human-paced, so the
# occasional npm ci is cheap). Node/npm come from docker/Dockerfile.
find_program(NPM_EXECUTABLE npm)
if(NPM_EXECUTABLE)
  set(WEBUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/webui)
  set(WEBUI_STAMP ${CMAKE_CURRENT_BINARY_DIR}/webui.stamp)
  file(GLOB_RECURSE WEBUI_SOURCES CONFIGURE_DEPENDS
       ${WEBUI_DIR}/src/*
       ${WEBUI_DIR}/index.html
       ${WEBUI_DIR}/vite.config.js
       ${WEBUI_DIR}/svelte.config.js
       ${WEBUI_DIR}/tsconfig.json
       ${WEBUI_DIR}/package.json)
  add_custom_command(
    OUTPUT ${WEBUI_STAMP}
    COMMAND ${NPM_EXECUTABLE} ci --no-audit --no-fund
    COMMAND ${NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E touch ${WEBUI_STAMP}
    WORKING_DIRECTORY ${WEBUI_DIR}
    DEPENDS ${WEBUI_SOURCES} ${WEBUI_DIR}/package-lock.json
    COMMENT "Building pedalboard web UI (Svelte/Vite) -> web/"
    VERBATIM)
  add_custom_target(pedalboard_webui ALL DEPENDS ${WEBUI_STAMP})
  add_dependencies(pedalboard pedalboard_webui)
else()
  message(WARNING
    "npm not found: skipping the pedalboard web UI build. The web/ dir may be "
    "missing or stale until you build webui/ manually (cd webui && npm ci && npm run build).")
endif()

# --- macOS sim: stage web/ + rigs/ + presets/ + setlists/ next to the binary -
# On the Pi, deploy.sh rsyncs these alongside the binary; the app resolves them
# relative to the executable. For the local Mac sim there's no deploy step, so
# copy them next to the JUCE artefact after the build. (web/ is produced by
# pedalboard_webui, which pedalboard already depends on.)
if(APPLE)
  add_custom_command(TARGET pedalboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web $<TARGET_FILE_DIR:pedalboard>/web
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/rigs $<TARGET_FILE_DIR:pedalboard>/rigs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/presets $<TARGET_FILE_DIR:pedalboard>/presets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/setlists $<TARGET_FILE_DIR:pedalboard>/setlists
    COMMENT "Staging web/ + rigs/ + presets/ + setlists/ next to the pedalboard binary (macOS sim)"
    VERBATIM)
endif()

# --- offline chain benchmark (no hardware/ALSA; builds + runs on the Mac) ----
juce_add_console_app(pedalboard_render PRODUCT_NAME "pedalboard_render")
target_sources(pedalboard_render PRIVATE pedalboard_render.cpp)
target_compile_definitions(pedalboard_render PRIVATE
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_include_directories(pedalboard_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pedalboard_render PRIVATE
  nam_core
  juce::juce_core
  juce::juce_audio_basics
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
  Threads::Threads
)

# ---- tests (Catch2 + ctest) --------------------------------------------------
# include(CTest) defines the standard BUILD_TESTING option (default ON) and
# enables ctest at the top level. The suite is hardware-free (no ALSA/GPIO/HAL):
# it links the effects + chain + registry directly, so it builds and runs both
# on the Mac and in the Docker arm64 image. -DBUILD_TESTING=OFF skips it.
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

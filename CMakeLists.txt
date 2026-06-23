# pedalboard -- the worship multi-effect pedalboard + web UI.
#
# Links pistomp_hal for the hardware drivers + LVGL, JUCE's dsp/audio modules for
# the heavy effects, nam_core for the amp, and cpp-httplib (declared in the root
# CMakeLists) for the web control surface.
#
# Added AFTER app/ and pistomp-hal/ in the root CMakeLists so the juce:: and
# pistomp_hal targets already exist.

find_package(Threads REQUIRED)
find_library(ASOUND_LIB asound REQUIRED)

# --- the live pedal (Pi: ALSA audio + hardware + web) -----------------------
juce_add_console_app(pedalboard PRODUCT_NAME "pedalboard")
target_sources(pedalboard PRIVATE
  pedalboard.cpp
  ui/ui_controller.cpp
  web_server.cpp
  presets.cpp
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
  ${ASOUND_LIB}
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

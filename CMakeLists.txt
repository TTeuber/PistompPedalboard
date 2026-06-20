# pedalboard -- the worship multi-effect pedalboard + web UI.
#
# Reuses the demo's hardware drivers verbatim (same pattern as nam/CMakeLists),
# the shared `lvgl` target (created in demo/, which runs first), JUCE's dsp/audio
# modules for the heavy effects, nam_core for the amp, and cpp-httplib (declared
# in the root CMakeLists) for the web control surface.
#
# Added AFTER app/ and demo/ in the root CMakeLists so the juce:: and lvgl targets
# already exist.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod)   # libgpiod v1 C API
find_package(Threads REQUIRED)
find_library(ASOUND_LIB asound REQUIRED)

set(DEMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../demo)

# --- the live pedal (Pi: ALSA audio + hardware + web) -----------------------
juce_add_console_app(pedalboard PRODUCT_NAME "pedalboard")
target_sources(pedalboard PRIVATE
  pedalboard.cpp
  pedal_ui.cpp
  web_server.cpp
  presets.cpp
  ${DEMO_DIR}/ili9341.cpp
  ${DEMO_DIR}/lvgl_display.cpp
  ${DEMO_DIR}/encoder.cpp
  ${DEMO_DIR}/footswitch.cpp
  ${DEMO_DIR}/leds.cpp
)
target_compile_definitions(pedalboard PRIVATE
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_include_directories(pedalboard PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${DEMO_DIR} ${GPIOD_INCLUDE_DIRS})
target_link_libraries(pedalboard PRIVATE
  nam_core
  lvgl
  httplib::httplib
  juce::juce_core
  juce::juce_audio_basics
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
  ${ASOUND_LIB}
  ${GPIOD_LIBRARIES}
  Threads::Threads
)

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

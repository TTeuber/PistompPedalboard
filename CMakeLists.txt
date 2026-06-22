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
  pedal_ui.cpp
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

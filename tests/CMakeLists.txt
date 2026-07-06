# pedalboard tests -- Catch2 + ctest, hardware-free.
#
# Links the effect headers, chain, factory, and the shared fx_registry.cpp --
# NOT pedalboard.cpp (which owns main() and the HAL wiring) -- so the suite
# builds without ALSA/GPIO and runs anywhere. Every kind registered in
# fx_registry.cpp is swept automatically; adding an effect to the registry adds
# it to the tests with zero new test code.

find_package(Threads REQUIRED)

# Catch2 v3, pinned by tag -- same FetchContent pattern as JUCE/httplib/namcore
# in the root CMakeLists. Catch2::Catch2WithMain supplies main().
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

# juce_add_console_app (not add_executable) so the target gets the same JUCE
# module setup as pedalboard/pedalboard_render; JUCE console apps don't
# generate a main, so Catch2's is the only one.
juce_add_console_app(pedalboard_tests PRODUCT_NAME "pedalboard_tests")
target_sources(pedalboard_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../fx_registry.cpp
  test_registry.cpp
  test_dsp_invariants.cpp
)
target_compile_definitions(pedalboard_tests PRIVATE
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_include_directories(pedalboard_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pedalboard_tests PRIVATE
  Catch2::Catch2WithMain
  juce::juce_core
  juce::juce_audio_basics
  juce::juce_dsp
  juce::juce_recommended_config_flags
  Threads::Threads
)

# Register each TEST_CASE as its own ctest test (run `ctest` in the build dir).
catch_discover_tests(pedalboard_tests)

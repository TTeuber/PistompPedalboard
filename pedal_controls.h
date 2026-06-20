// pedal_controls.h -- global (non-per-effect) shared state for the pedalboard.
//
// Per-effect parameters live in each Effect's Param atomics (effect.h). This
// struct holds the handful of GLOBAL atomics every domain touches, same lock-free
// discipline as nam_controls.h:
//   * audio thread READS masterLevel/bypassed each block, WRITES telemetry,
//   * input thread WRITES masterLevel/bypassed (encoders + footswitch),
//   * web + UI threads READ/WRITE as needed.

#pragma once
#include <atomic>

struct PedalControls {
  std::atomic<float> masterLevel{1.0f};  // post-chain master volume (0..2)
  std::atomic<bool>  bypassed{false};     // true = whole chain bypassed (dry DI)

  // Telemetry: written by the audio thread, read by the UI/web.
  std::atomic<unsigned> dspPermille{0};   // chain process() time / period * 1000
  std::atomic<unsigned> xruns{0};

  std::atomic<bool> running{true};        // false = every domain shuts down
};

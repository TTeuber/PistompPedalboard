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
  PedalControls() { for (auto& f : fsEngaged) f.store(true); }  // engaged by default

  std::atomic<float> masterLevel{1.0f};  // post-chain master volume (0..2)
  std::atomic<bool>  bypassed{false};     // true = whole chain bypassed (dry DI)

  // Latched footswitch state, FS1..FS4 (engaged-by-default, like the physical
  // switches). Lives here -- not privately in the UiController -- so the web
  // server can toggle a footswitch too and BOTH surfaces (device LEDs/LCD + the
  // browser) agree on one truth. Written by the input/UI thread and the web
  // thread; read anywhere. Apply a change with applyFootswitch()
  // which syncs the `enabled` flag of every effect bound to that switch.
  // (Initialized in the constructor: aggregate-initializing an atomic array is
  // ill-formed before C++20.)
  std::atomic<bool> fsEngaged[4];

  // Telemetry: written by the audio thread, read by the UI/web.
  std::atomic<unsigned> dspPermille{0};   // chain process() time / period * 1000
  std::atomic<unsigned> xruns{0};

  // Input level peak-hold per channel (0 = L/input1, 1 = R/input2), |sample| in
  // [0,1]. Written by the audio thread (running max), read-and-cleared by the UI
  // thread to drive the input-level meter LEDs in the simulator (where there's no
  // analog ADC detector). See pedalboard/input_vu.h.
  std::atomic<float> inPeak[2]{};

  // Web meter peak-holds, |sample| in [0,1], read-and-cleared by the web layer
  // (/api/meters). These are SEPARATE from inPeak above on purpose: a peak-hold
  // has one consumer (whoever clears it steals it), and the device LED meter
  // already owns inPeak. So the audio thread writes the input peak into BOTH --
  // inPeak for the device, inPeakWeb for the browser -- so neither starves the
  // other. outPeak is post-master and web-only.
  std::atomic<float> inPeakWeb[2]{};
  std::atomic<float> outPeak[2]{};

  std::atomic<bool> running{true};        // false = every domain shuts down
};

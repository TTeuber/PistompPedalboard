// input_vu.h — input-level meter policy (shared, platform-independent).
//
// The pi-Stomp's two top NeoPixels are input level meters: green with signal,
// orange approaching clip, red when clipping. This header holds the *policy* --
// the state machine, anti-flicker averaging and colours -- independent of where
// the level reading comes from. Two sources feed it the same way:
//   * hardware: a pre-gain analog peak-detector on MCP3008 ch 6/7 (raw 0..1023,
//     rectified about a baseline), see pistomp-hal/input_level.*;
//   * simulator: the digital audio peak computed in the audio callback (0..1).
// Each source supplies an amplitude and matching VuThresholds (same unit), so the
// VuMeter math is identical on both and can be developed/tuned in the simulator.
//
// Generalized from the original pi-stomp pistomp/analogVU.py: a short averaging
// window drives the on-states (Sig/Warn/Clip) so transients light immediately,
// and a long window guards the off-state so the meter doesn't strobe off on dips.

#pragma once
#include <cmath>
#include <cstdint>

enum class VuState { Off, Sig, Warn, Clip };

// Signal-present / warning / clipping thresholds, in the source's native unit
// (ADC counts for hardware, linear peak 0..1 for the simulator).
struct VuThresholds { float sig, warn, clip; };

// Thresholds for the hardware ADC source (raw count space). Ported from
// analogVU.recalibrate(): dB relative to the supply, biased by input gain, around
// an ADC baseline. The matching amplitude is |baseline - raw| + baseline.
// TODO: feed real ALSA capture volume as inputGain (the app has no mixer yet).
inline VuThresholds adcThresholds(float inputGain = 0.0f, int baseline = 512) {
  const float unitsPerVolt = 512.0f / 1.665f;   // (ADC range/2) / (supply/2)
  auto t = [&](float db) {
    return (float)baseline + std::pow(10.0f, db / 20.0f) * unitsPerVolt;
  };
  return { t(-39.0f - inputGain), t(-20.0f - inputGain), t(-15.0f - inputGain) };
}

// Thresholds for the simulator's digital peak source (linear peak 0..1, dBFS).
// Tune these in the sim until the green/orange/red transitions feel right.
inline VuThresholds digitalThresholds() {
  auto t = [](float dbfs) { return std::pow(10.0f, dbfs / 20.0f); };
  return { t(-40.0f), t(-12.0f), t(-1.0f) };    // sig, warn, clip
}

// One input's meter: feed it an amplitude per UI tick, get back a VuState.
class VuMeter {
public:
  // Window lengths in samples (UI loop ~20 ms -> ~40 ms fast / ~500 ms slow,
  // matching the original's 4/50 samples at 10 ms).
  static constexpr int kFast = 2;
  static constexpr int kSlow = 25;

  VuState update(float amp, const VuThresholds& th) {
    push(fast_, fastSum_, kFast, fastN_, amp);
    push(slow_, slowSum_, kSlow, slowN_, amp);
    const float fastAvg = fastSum_ / (float)kFast;
    const float slowAvg = slowSum_ / (float)kSlow;

    // Off is governed by the slow window (lagged) so brief dips don't blink it
    // off; the on-states track the fast window so transients show at once.
    VuState s = state_;
    if (slowAvg < th.sig)        s = VuState::Off;
    else if (fastAvg >= th.clip) s = VuState::Clip;
    else if (fastAvg >= th.warn) s = VuState::Warn;
    else if (fastAvg >= th.sig)  s = VuState::Sig;
    state_ = s;
    return s;
  }

  // State -> RGB, scaled to the same on-brightness the footswitch LEDs use so the
  // strip reads at a consistent level (the overlay runs at full brightness).
  static void color(VuState s, uint8_t& r, uint8_t& g, uint8_t& b) {
    constexpr int pct = 40;   // matches kLedOnPct in ui_controller.cpp
    auto k = [](uint8_t v) -> uint8_t { return (uint8_t)(v * pct / 100); };
    switch (s) {
      case VuState::Sig:  r = k(34);  g = k(139); b = k(34); break;  // forestgreen
      case VuState::Warn: r = k(255); g = k(165); b = k(0);  break;  // orange
      case VuState::Clip: r = k(255); g = k(0);   b = k(0);  break;  // red
      case VuState::Off:  default: r = g = b = 0; break;
    }
  }

private:
  // Fixed-length running mean over a ring of zeros (so it ramps up from silence).
  static void push(float* ring, float& sum, int len, int& head, float v) {
    sum += v - ring[head];
    ring[head] = v;
    head = (head + 1) % len;
  }

  float fast_[kFast] = {0};
  float slow_[kSlow] = {0};
  float fastSum_ = 0.0f, slowSum_ = 0.0f;
  int   fastN_ = 0, slowN_ = 0;   // ring heads
  VuState state_ = VuState::Off;
};

// effects/mod_dsp.h -- shared building blocks for the modulation pedals.
//
// Small, allocation-free, real-time-safe DSP structs used by the modulation
// collection (chorus.h, phaser.h, flanger.h, uni_vibe.h, dimension.h,
// rotary.h). Everything runs lock-free per sample; nothing here is an Effect --
// these are the guts the pedals compose. (The phase-accumulator Lfo itself
// lives in dsp_util.h: it is shared with the delay collection.)

#pragma once

#include "../dsp_util.h"
#include "../tempo.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace md {

// ---------------------------------------------------------------------------
// The sync-or-knob RATE every synced modulation shares (Tremolo's inline
// recipe promoted): one LFO cycle per division at the board tempo when Sync is
// on, the manual Rate knob otherwise. Counterpart of dly::targetMs.
inline float rateHz(bool sync, int div, float knobHz) noexcept {
  return sync ? 1000.0f / tempo::divisionMs(div, tempo::bpm()) : knobHz;
}

// ---------------------------------------------------------------------------
// Extra LFO shapes for a phase in [0,1) (sine/raisedCos live on Lfo itself).
// tri: bipolar triangle, -1 at the wrap, +1 at half-phase.
inline float tri(float p) noexcept {
  return 1.0f - 4.0f * std::fabs(p - 0.5f);
}
// skewedCos: unipolar raised-cosine raised to a power -- the lamp/LDR throb of
// a Uni-Vibe. k > 1 narrows the bright bloom and stretches the dark tail.
inline float skewedCos(float p, float k) noexcept {
  return std::pow(Lfo::raisedCos(p), k);
}

// ---------------------------------------------------------------------------
// First-order allpass section -- the phaser/vibe atom.
//
//   y[n] = a*x[n] + x[n-1] - a*y[n-1]
//
// Flat magnitude, phase swings 0..-180 degrees through fc; cascaded pairs
// mixed against the dry signal carve one notch per pair. coef() is split from
// process() so an N-stage chain computes one tan() per sample and shares it
// (or, vibe-style, one per staggered stage).
struct Allpass1 {
  float a = 0.0f, x1 = 0.0f, y1 = 0.0f;

  // fc is clamped to [30 Hz, 0.45*fs], which keeps |a| < 1 -- the section (and
  // any chain of them) is unconditionally stable no matter what the sweep does.
  static float coef(float fc, double fs) noexcept {
    fc = std::clamp(fc, 30.0f, (float)(0.45 * fs));
    const float t = std::tan((float)M_PI * fc / (float)fs);
    return (t - 1.0f) / (t + 1.0f);
  }
  void setCoef(float c) noexcept { a = c; }
  void reset() noexcept { x1 = y1 = 0.0f; }

  float process(float x) noexcept {
    const float y = a * (x - y1) + x1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// ---------------------------------------------------------------------------
// Modulated fractional delay tap (single channel) -- the chorus/flanger/
// dimension/rotary voice. Same shape as rv::Delay1, copied locally on purpose
// so the modulation pedals don't drag in the reverb collection's header (the
// delay pedals' HighPass1 precedent).
struct ModDelay {
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line;

  void prepare(const juce::dsp::ProcessSpec& spec, int maxSamples) {
    line.prepare(spec);
    line.setMaximumDelayInSamples(maxSamples + 8);
    line.reset();
  }
  void reset() noexcept { line.reset(); }
  void write(float x) noexcept { line.pushSample(0, x); }
  // Main read: advances the read pointer (call exactly once per sample).
  float read(float delaySamps) noexcept { return line.popSample(0, delaySamps); }
  // Extra tap: does NOT advance, so one line can feed several voices a sample.
  float tap(float delaySamps) noexcept {
    return line.popSample(0, delaySamps, false);
  }
};

}  // namespace md
}  // namespace fx

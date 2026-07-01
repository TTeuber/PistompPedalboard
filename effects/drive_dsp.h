// effects/drive_dsp.h -- shared building blocks for the drive/distortion pedals.
//
// Small, allocation-free, real-time-safe DSP structs used by the five gain
// pedals (overdrive.h, gold_drive.h, distortion.h, fuzz.h, sustainer.h).
// Everything sizes itself in prepare() and runs lock-free per sample, composing
// Biquad/OnePole/Smoother from dsp_util.h. Nothing here is an Effect -- these
// are the guts the pedals wire together.
//
// The one structural idiom to know: each pedal's ENTIRE nonlinear path (input
// highpass, gain, clipping stage(s), inter-stage filters) lives inside the
// shaper functor handed to OversamplerIIR, so all of it runs at the oversampled
// rate. Any filter inside the functor must therefore set its cutoff against
// baseFs * factor, not baseFs.

#pragma once

#include "../dsp_util.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace drv {

// ---------------------------------------------------------------------------
// Waveshaper primitives. All stateless, all inlined into the per-sample loop.

// Smooth symmetric saturation -- the "amp pushed harder" curve.
inline float softClip(float x) noexcept { return std::tanh(x); }

// Diode-style hard limiter with independent rails (tNeg < 0 < tPos). Unequal
// rails give even harmonics; anything using them MUST be followed by DcBlock.
inline float hardClip(float x, float tPos, float tNeg) noexcept {
  return std::clamp(x, tNeg, tPos);
}

// Saturation around a shifted operating point (a mis-biased transistor). The
// static offset's DC is pre-subtracted; the *dynamic* DC that appears when the
// bias moves with the envelope is the DcBlock's job.
inline float biasClip(float x, float b) noexcept {
  return std::tanh(x + b) - std::tanh(b);
}

// ---------------------------------------------------------------------------
// One-pole highpass built from OnePole's lowpass (x - lowpass(x)). Same trick
// as rv::HighCut, copied locally so the drives don't drag in reverb_dsp.h.
struct HighPass1 {
  OnePole lp;
  void setCutoff(double fc, double fs) noexcept { lp.setCutoff(fc, fs); }
  void reset() noexcept { lp.reset(); }
  float process(float x) noexcept { return x - lp.process(x); }
};

// ---------------------------------------------------------------------------
// DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1], a first-order highpass at fc.
// Sits right after every clipping stage at base rate. Mandatory when the
// shaper is asymmetric (unequal hardClip rails, moving biasClip) -- those
// rectify the signal and push a DC term into everything downstream.
struct DcBlock {
  float R = 0.998f, x1 = 0.0f, y1 = 0.0f;

  void prepare(double fs, double fc = 15.0) noexcept {
    R = float(std::exp(-2.0 * M_PI * fc / fs));
    reset();
  }
  void reset() noexcept { x1 = y1 = 0.0f; }

  float process(float x) noexcept {
    const float y = x - x1 + R * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// ---------------------------------------------------------------------------
// Envelope follower: one-pole on |x| with separate attack/release. Drives the
// Fuzz's bias shift (hit hard -> bias slams over -> gated sputter; input
// decays -> bias relaxes -> the note "cleans up").
struct EnvFollower {
  float aA = 0.0f, aR = 0.0f, e = 0.0f;

  void prepare(double fs, double atkMs, double relMs) noexcept {
    aA = float(std::exp(-1.0 / (0.001 * atkMs * fs)));
    aR = float(std::exp(-1.0 / (0.001 * relMs * fs)));
    e = 0.0f;
  }
  void reset() noexcept { e = 0.0f; }

  float process(float x) noexcept {
    const float r = std::fabs(x);
    const float a = r > e ? aA : aR;
    e = r + a * (e - r);
    return e;
  }
};

// ---------------------------------------------------------------------------
// Big Muff tilt tone: crossfade a lowpass against a highpass with a gap
// between the corners, so the middle of the knob leaves a mid scoop (~-9 dB
// around ~900 Hz at t=0.5 -- the classic voicing). 0 = woolly lows only,
// 1 = cutting highs only.
struct ScoopTone {
  OnePole lp;      // lows arm
  OnePole hpLp;    // highs arm (highpass = x - lowpass)

  void prepare(double fs) noexcept {
    lp.setCutoff(500.0, fs);
    hpLp.setCutoff(1600.0, fs);
    reset();
  }
  void reset() noexcept {
    lp.reset();
    hpLp.reset();
  }

  float process(float x, float t) noexcept {
    const float lo = lp.process(x);
    const float hi = x - hpLp.process(x);
    return (1.0f - t) * lo + t * hi;
  }
};

// ---------------------------------------------------------------------------
// N-x oversampler (N = 2 or 4) around a sample-wise nonlinearity. Replaces the
// old Oversampler2x, whose single 12 dB/oct biquad anti-alias was nearly
// decorative. Up/down filters here are 8th-order Butterworth lowpasses (four
// cascaded RBJ biquad sections) at 0.45 * baseFs, designed at the oversampled
// rate: ~35 dB down at the worst-case fold vs ~9 dB before, and much deeper
// for everything folding into the audible band.
//
// IIR on purpose: the chain has no latency compensation and Effect::run()
// crossfades wet against dry on every bypass toggle, so a linear-phase FIR's
// bulk delay would comb against the dry path. Butterworth adds no bulk delay,
// only phase warp near the corner -- inaudible after a distortion stage.
//
// Holds the state for ONE channel; instantiate one per channel. `shape` is a
// sample->sample functor and is called for every subsample (including the
// stuffed zeros), so stateful filters inside it advance correctly.
struct OversamplerIIR {
  static constexpr int kSections = 4;
  Biquad up[kSections], down[kSections];
  int factor = 2;

  void prepare(double baseFs, int f) noexcept {
    factor = f;
    const double os = baseFs * f;
    // Butterworth section Qs for order 8: Qk = 1 / (2*cos((2k-1)*pi/16)).
    static constexpr double kQ[kSections] = {0.50979558, 0.60134489,
                                             0.89997622, 2.56291545};
    for (int i = 0; i < kSections; i++) {
      up[i].setLowpass(baseFs * 0.45, os, kQ[i]);
      down[i].setLowpass(baseFs * 0.45, os, kQ[i]);
      up[i].reset();
      down[i].reset();
    }
  }

  void reset() noexcept {
    for (int i = 0; i < kSections; i++) {
      up[i].reset();
      down[i].reset();
    }
  }

  template <typename Shaper>
  float process(float x, Shaper shape) noexcept {
    // Zero-stuff upsample (x, 0, ...) with *factor gain compensation, shape,
    // anti-alias, then decimate by keeping the first subsample of each group.
    float out = 0.0f;
    for (int k = 0; k < factor; k++) {
      float u = (k == 0) ? float(factor) * x : 0.0f;
      for (int i = 0; i < kSections; i++) u = up[i].process(u);
      float v = shape(u);
      for (int i = 0; i < kSections; i++) v = down[i].process(v);
      if (k == 0) out = v;
    }
    return out;
  }
};

}  // namespace drv
}  // namespace fx

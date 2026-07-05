// effects/delay_dsp.h -- shared building blocks for the delay pedals.
//
// Small, allocation-free, real-time-safe DSP structs used by the delay
// collection (delay.h, tape_echo.h, analog_delay.h, multitap_delay.h,
// reverse_delay.h, ambient_delay.h). Everything runs lock-free per sample;
// nothing here is an Effect -- these are the guts the pedals compose.
//
// The FREEZE recipe (used by every freezable delay, always crossfaded by the
// Freeze ramp `frz`, never switched):
//   * input injection gain      = 1 - frz
//   * loop gain                 = fb*(1-frz) + 1.0*frz   (exactly unity frozen,
//                                 whatever the knob -- including fb > 100%)
//   * lossy loop coloring       = colored*(1-frz) + raw*frz  (damping and
//                                 saturation fade out so the frozen loop is
//                                 lossless; LOSSLESS elements like allpass
//                                 diffusers may stay in)
//   * softLimit stays engaged always (safety only; transparent below ~-6 dBFS)
// Because delays report hasTails(), a frozen loop keeps sounding through
// bypass -- same behavior as Reverb Freeze; treat as a feature.

#pragma once

#include "../dsp_util.h"
#include "../tempo.h"

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace dly {

// ---------------------------------------------------------------------------
// Rate-capped one-pole glide -- the delay-time smoother promoted out of
// delay.h. The one-pole eases small wiggles; the +/-maxSlew cap turns big
// jumps (Sync/Div changes, knob sweeps) into a bounded tape-style pitch bend
// and keeps a read tap from ever outrunning the write head.
struct GlideValue {
  float v = 0.0f, k = 0.0f, maxSlew = 0.5f;

  void prepare(double fs, double tcMs = 100.0, float slew = 0.5f) noexcept {
    k = float(1.0 - std::exp(-1.0 / (0.001 * tcMs * fs)));
    maxSlew = slew;
  }
  void snap(float x) noexcept { v = x; }
  float next(float target) noexcept {
    float d = (target - v) * k;
    if (d > maxSlew) d = maxSlew;
    if (d < -maxSlew) d = -maxSlew;
    v += d;
    return v;
  }
};

// ---------------------------------------------------------------------------
// Phase-accumulator LFO -- promoted to dsp_util.h when the modulation
// collection became its second collection of consumers; aliased here so the
// delay pedals keep reading dly::Lfo.
using ::Lfo;

// ---------------------------------------------------------------------------
// Lowpassed white noise for tape flutter's random component. xorshift32 mapped
// to +/-1 through a one-pole -- RT-safe (no rand(), no syscalls).
struct Jitter {
  uint32_t s = 0x12345u;
  OnePole lp;

  void prepare(double fs, double fc = 8.0) noexcept {
    lp.setCutoff(fc, fs);
    lp.reset();
  }
  float next() noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    // Map to +/-1; the lowpass tames it to a slow wander.
    const float white = (float)(int32_t)s * (1.0f / 2147483648.0f);
    return lp.process(white);
  }
};

// ---------------------------------------------------------------------------
// Tape transport modulation: wow (slow speed drift) + flutter (fast jitter),
// returned as milliseconds of read-tap deviation. Two instances with different
// phase offsets make L/R "tracks" drift apart for free stereo.
// Worst-case swing is ~+/-4.3 ms at full depths -- allocate a ~5 ms guard on
// the delay line.
struct WowFlutter {
  static constexpr float kGuardMs = 5.0f;

  void prepare(double fs, float wowPhase, float flutterPhase) noexcept {
    wow_.setRate(0.45f, fs);
    wow_.setPhase(wowPhase);
    drift_.setRate(0.11f, fs);
    drift_.setPhase(wowPhase + 0.5f);
    flutter_.setRate(6.3f, fs);
    flutter_.setPhase(flutterPhase);
    jitter_.prepare(fs);
  }

  // wowAmt/flutterAmt are the 0..1 knob values.
  float nextMs(float wowAmt, float flutterAmt) noexcept {
    const float wow = Lfo::sine(wow_.next()) * 3.5f +
                      Lfo::sine(drift_.next()) * 0.8f;
    const float flt = Lfo::sine(flutter_.next()) * 0.25f +
                      jitter_.next() * 0.15f;
    return wow * wowAmt + flt * flutterAmt;
  }

private:
  Lfo wow_, drift_, flutter_;
  Jitter jitter_;
};

// ---------------------------------------------------------------------------
// Loop safety limiter. tanh(g*x)/g has exactly unity small-signal slope, so it
// is transparent below ~-6 dBFS and ceilings at 2.5 (+8 dB). Sits in every
// pedal's loop write path: feedback > 100% settles into bounded, darkening
// self-oscillation instead of blowing up. Mild and post-lowpass, so base rate
// is fine (contrast drive_dsp.h, where hard clipping needs oversampling).
inline float softLimit(float x) noexcept { return std::tanh(0.4f * x) * 2.5f; }

// ---------------------------------------------------------------------------
// Click-free freeze ramp: a ~50 ms one-pole from the toggle param, fully
// settled in ~250 ms. Feed its output into the recipe at the top of this file.
struct Freeze {
  Smoother s;

  void prepare(double fs) noexcept {
    s.prepare(fs, 50.0);
    s.snap(0.0f);
  }
  float next(bool on) noexcept { return s.next(on ? 1.0f : 0.0f); }
};

// ---------------------------------------------------------------------------
// One-pole highpass built from OnePole's lowpass (x - lowpass(x)). Same trick
// as rv::HighCut/drv::HighPass1, copied locally so the delays don't drag in
// the other collections' headers. Used in every feedback loop so repeats shed
// lows instead of piling them up.
struct HighPass1 {
  OnePole lp;
  void setCutoff(double fc, double fs) noexcept { lp.setCutoff(fc, fs); }
  void reset() noexcept { lp.reset(); }
  float process(float x) noexcept { return x - lp.process(x); }
};

// ---------------------------------------------------------------------------
// The sync-or-knob time target every delay shares: the synced division at the
// board tempo when Sync is on, the manual Time knob otherwise.
inline float targetMs(bool sync, int div, float knobMs) noexcept {
  return sync ? tempo::divisionMs(div, tempo::bpm()) : knobMs;
}

}  // namespace dly
}  // namespace fx

// effects/reverb_dsp.h -- shared building blocks for the algorithmic reverbs.
//
// Small, allocation-free, real-time-safe DSP structs used by the three reverb
// pedals (hall_reverb.h, plate_reverb.h, shimmer_reverb.h). Everything allocates
// in prepare() and runs lock-free per sample, matching the delay/flanger idiom
// (juce::dsp::DelayLine for the buffers, hand-rolled LFOs, OnePole/Biquad from
// dsp_util.h). Nothing here is an Effect -- these are the guts the pedals compose.

#pragma once

#include "../dsp_util.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace rv {

// ---------------------------------------------------------------------------
// Schroeder / Dattorro allpass diffuser on a fractional delay line.
//
//   w[n] = x[n] + g * w[n-m]
//   y[n] = w[n-m] - g * w[n]
//
// Magnitude response is flat (allpass); it just smears phase, turning a sparse
// echo train into a smooth diffuse tail. The delay is read fractionally so the
// tank allpasses can be gently modulated to break up metallic ringing.
struct Allpass {
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line;
  float g = 0.5f;

  void prepare(const juce::dsp::ProcessSpec& spec, int maxSamples) {
    line.prepare(spec);
    line.setMaximumDelayInSamples(maxSamples + 8);
    line.reset();
  }
  void reset() noexcept { line.reset(); }

  float process(float x, float delaySamps) noexcept {
    const float wd = line.popSample(0, delaySamps);
    const float w = x + g * wd;
    line.pushSample(0, w);
    return wd - g * w;
  }
};

// Plain fractional delay tap (single channel), for the Dattorro tank delays and
// the pre-delay. Thin wrapper so the reverbs read uniformly.
struct Delay1 {
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line;

  void prepare(const juce::dsp::ProcessSpec& spec, int maxSamples) {
    line.prepare(spec);
    line.setMaximumDelayInSamples(maxSamples + 8);
    line.reset();
  }
  void reset() noexcept { line.reset(); }
  void write(float x) noexcept { line.pushSample(0, x); }
  // Main read: advances the read pointer (call exactly once per sample per line).
  float read(float delaySamps) noexcept { return line.popSample(0, delaySamps); }
  // Extra output tap: does NOT advance the read pointer, so a line can be read
  // at several interior offsets in one sample (Dattorro output taps).
  float tap(float delaySamps) noexcept {
    return line.popSample(0, delaySamps, false);
  }
};

// One-pole highpass built from OnePole's lowpass (x - lowpass(x)). Used as the
// low-cut into a reverb tank so big washes don't turn to mud.
struct HighCut {
  OnePole lp;
  void setCutoff(double fc, double fs) noexcept { lp.setCutoff(fc, fs); }
  void reset() noexcept { lp.reset(); }
  float process(float x) noexcept { return x - lp.process(x); }
};

// ---------------------------------------------------------------------------
// 8-line feedback delay network -- the core of the Hall and Shimmer reverbs.
//
// Eight mutually-incommensurate delay lines are mixed every sample by an 8x8
// Householder matrix (orthogonal, so it preserves energy and never colours the
// tail), each with a one-pole lowpass for frequency-dependent damping and a slow
// LFO on its read tap for the lush Valhalla-style chorusing. Per-line feedback
// gain is derived from the target RT60 so decay time is a real seconds value.
struct FDN8 {
  static constexpr int N = 8;

  void prepare(double fs, double maxSizeScale) {
    fs_ = fs;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = fs;
    spec.maximumBlockSize = 1;   // we push/pop one sample at a time
    spec.numChannels = N;
    lines_.prepare(spec);
    // Allocate for the largest size plus the modulation swing and a guard.
    double maxMs = kBaseMs[N - 1] * maxSizeScale;
    int maxSamps = (int)std::ceil(fs * (maxMs / 1000.0)) + (int)kMaxModSamps(fs) + 8;
    lines_.setMaximumDelayInSamples(maxSamps);
    reset();
    for (int i = 0; i < N; i++) {
      delayCur_[i] = (float)(kBaseMs[i] * 0.001 * fs);
      delayTgt_[i] = delayCur_[i];
    }
    glide_ = (float)(1.0 - std::exp(-1.0 / (0.05 * fs)));  // ~50 ms size glide
  }

  void reset() noexcept {
    lines_.reset();
    for (int i = 0; i < N; i++) { damp_[i].reset(); lfoPhase_[i] = 0.1f * (float)i; }
  }

  // Called once per block. sizeScale ~0.25..2.0, rt60 in seconds, dampFc the
  // damping lowpass cutoff (Hz), modDepthMs the LFO swing, modRateHz the speed.
  void setParams(float sizeScale, float rt60, double dampFc, float modDepthMs,
                 float modRateHz) noexcept {
    modDepth_ = modDepthMs * 0.001f * (float)fs_;
    for (int i = 0; i < N; i++) {
      delayTgt_[i] = (float)(kBaseMs[i] * sizeScale * 0.001 * fs_);
      const double dSec = delayTgt_[i] / fs_;
      // g so a signal fed round the loop reaches -60 dB after rt60 seconds.
      g_[i] = (float)std::pow(10.0, -3.0 * dSec / std::max(0.05f, rt60));
      damp_[i].setCutoff(std::min(dampFc, fs_ * 0.49), fs_);
      lfoInc_[i] = modRateHz * kRateSpread[i] / (float)fs_;
    }
  }

  // One sample in, stereo wash out (added to whatever the caller passes for
  // outL/outR conceptually -- here it writes them). `in` is the already-diffused
  // mono injection signal.
  void process(float in, float& outL, float& outR) noexcept {
    float s[N];
    for (int i = 0; i < N; i++) {
      delayCur_[i] += (delayTgt_[i] - delayCur_[i]) * glide_;
      lfoPhase_[i] += lfoInc_[i];
      if (lfoPhase_[i] >= 1.0f) lfoPhase_[i] -= 1.0f;
      const float mod =
          modDepth_ * 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * lfoPhase_[i]));
      float x = lines_.popSample(i, delayCur_[i] + mod);
      s[i] = damp_[i].process(x);
    }
    // Stereo taps: even lines left, odd lines right.
    outL = (s[0] + s[2] + s[4] + s[6]) * 0.5f;
    outR = (s[1] + s[3] + s[5] + s[7]) * 0.5f;

    // Apply decay gain, then the Householder reflection y = v - (2/N)*sum(v).
    float vsum = 0.0f;
    float v[N];
    for (int i = 0; i < N; i++) { v[i] = s[i] * g_[i]; vsum += v[i]; }
    const float c = (2.0f / N) * vsum;
    for (int i = 0; i < N; i++)
      lines_.pushSample(i, kInject[i] * in + (v[i] - c));
  }

private:
  static double kMaxModSamps(double fs) { return 8.0 * 0.001 * fs; }

  // Delay lengths (ms) -- spread and mutually incommensurate for a dense tail.
  static constexpr double kBaseMs[N] = {19.1, 23.9, 29.3, 34.7,
                                        41.3, 47.9, 53.3, 59.9};
  // Per-line LFO rate multipliers so the eight chorus voices never lock up.
  static constexpr float kRateSpread[N] = {0.63f, 0.71f, 0.53f, 0.83f,
                                           0.59f, 0.77f, 0.67f, 0.89f};
  // Alternating-sign injection decorrelates the lines at the input.
  static constexpr float kInject[N] = {0.354f, -0.354f, 0.354f, -0.354f,
                                       0.354f, -0.354f, 0.354f, -0.354f};

  double fs_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      lines_;
  OnePole damp_[N];
  float g_[N] = {0};
  float delayCur_[N] = {0}, delayTgt_[N] = {0};
  float lfoPhase_[N] = {0}, lfoInc_[N] = {0};
  float modDepth_ = 0.0f, glide_ = 0.0f;
};

}  // namespace rv
}  // namespace fx

// effects/gate.h -- a simple noise gate, first in the chain.
//
// Single-coil/high-gain rigs hiss and hum between notes; a gate sits at the very
// front and mutes the signal when it falls below a threshold, so the drive/amp
// downstream aren't amplifying silence into noise. Hand-written (no JUCE needed):
// a fast peak-follower drives a gain that snaps open on a note and eases closed.
//
// Two student-friendly knobs: Threshold (how loud the signal must be to pass) and
// Release (how quickly it closes once you stop playing). Attack is fixed fast so
// note transients never get clipped off.

#pragma once

#include "../effect.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace fx {

class Gate : public Effect {
public:
  Gate() : Effect("gate", "Noise Gate") {
    thresh_  = addParam("threshold", "Threshold", "dB", -80, 0,   -60);
    release_ = addParam("release",   "Release",   "ms", 10,  800, 150);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    env_ = 0.0f;
    gain_ = 0.0f;
  }

  void process(float* L, float* R, int n) noexcept override {
    // Threshold dBFS -> linear amplitude. A small hysteresis (close 3 dB under
    // the open point) keeps the gate from chattering on notes that hover at the
    // threshold while they decay.
    const float open  = std::pow(10.0f, thresh_->get() / 20.0f);
    const float close = open * 0.7079f;  // ~ -3 dB

    // Per-block smoothing coefficients. Fast fixed attack (~2 ms) so picking
    // transients pass; release is the user knob (slower = longer tail).
    const float aEnv = expCoef(5.0f);                 // peak-follower decay
    const float aAtk = expCoef(2.0f);                 // gain opening
    const float aRel = expCoef(release_->get());      // gain closing

    float minGain = 1.0f;  // smallest gain this block -> most reduction
    for (int i = 0; i < n; i++) {
      // Peak envelope from whichever channel is louder (mono at this point).
      float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
      env_ = x > env_ ? x : aEnv * env_ + (1.0f - aEnv) * x;

      // Hysteretic target: open above `open`, stay open until below `close`.
      float target = (gain_ > 0.5f) ? (env_ > close ? 1.0f : 0.0f)
                                    : (env_ > open  ? 1.0f : 0.0f);
      float a = target > gain_ ? aAtk : aRel;
      gain_ = a * gain_ + (1.0f - a) * target;

      L[i] *= gain_;
      R[i] *= gain_;
      minGain = std::min(minGain, gain_);
    }

    // How far the gate pulled the signal down this block, dB (>= 0), for the
    // shared gain-reduction meter. Peak-held with a lock-free max so the meter
    // sees the deepest cut since it last read-and-cleared (see web_server.cpp);
    // a disabled gate never writes, so its held value decays to 0 on next read.
    float gr = std::clamp(-20.0f * std::log10(std::max(minGain, 1e-6f)),
                          0.0f, 24.0f);
    float cur = grDb_.load(std::memory_order_relaxed);
    while (gr > cur &&
           !grDb_.compare_exchange_weak(cur, gr, std::memory_order_relaxed)) {}
  }

  // Largest gain reduction (dB) since the last call; reading clears the peak hold
  // so a disabled gate decays to 0. Called by the web meter (web_server.cpp).
  float takeGrDb() noexcept { return grDb_.exchange(0.0f, std::memory_order_relaxed); }

private:
  // One-pole smoothing coefficient for a given time constant in milliseconds.
  float expCoef(float ms) const noexcept {
    return std::exp(-1.0f / (float(sr_) * 0.001f * std::max(ms, 0.1f)));
  }

  double sr_ = 48000.0;
  float env_ = 0.0f, gain_ = 0.0f;
  std::atomic<float> grDb_{0.0f};
  Param *thresh_, *release_;
};

}  // namespace fx

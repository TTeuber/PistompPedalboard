// effects/swell.h -- auto volume swell ("slow gear"): fades in every note.
//
// An envelope-driven volume that removes the pick attack: when the input rises
// above the (sensitivity-set) onset threshold, a gain multiplier ramps 0 -> 1
// over the Attack time, giving that violin/pad-like swell with no plucked
// transient. When the note decays below threshold the gain releases back to 0,
// re-arming the swell for the next note. Hand-written, RT-safe (one-pole ramps).

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <cmath>

namespace fx {

class Swell : public Effect {
public:
  Swell() : Effect("swell", "Auto-Swell") {
    sens_    = addParam("sens",    "Sensitivity", "%",  0, 100, 50);
    attack_  = addParam("attack",  "Attack",      "ms", 20, 2000, 350);
    release_ = addParam("release", "Release",     "ms", 50, 2000, 400);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    envLp_.reset();
    gain_ = 0.0f;
  }

  void process(float* L, float* R, int n) noexcept override {
    // Sensitivity maps to an onset threshold: 100% -> -60 dB (hair-trigger),
    // 0% -> -20 dB (only loud notes swell).
    const float thresh = std::pow(
        10.0f, (-60.0f + (1.0f - sens_->get() / 100.0f) * 40.0f) / 20.0f);
    // One-pole coefficients for the swell (attack) and fade-out (release) ramps.
    const float atk = rampCoef(attack_->get());
    const float rel = rampCoef(release_->get());
    envLp_.setCutoff(15.0, sr_);  // smooth amplitude follower

    for (int i = 0; i < n; i++) {
      float m = 0.5f * (L[i] + R[i]);
      float env = envLp_.process(std::fabs(m));
      float target = (env > thresh) ? 1.0f : 0.0f;
      float coef = (target > gain_) ? atk : rel;  // swell up vs. fade down
      gain_ += (target - gain_) * coef;
      L[i] *= gain_;
      R[i] *= gain_;
    }
  }

private:
  float rampCoef(float ms) const noexcept {
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * (float)sr_));
  }

  double sr_ = 48000.0;
  OnePole envLp_;
  float gain_ = 0.0f;
  Param *sens_, *attack_, *release_;
};

}  // namespace fx

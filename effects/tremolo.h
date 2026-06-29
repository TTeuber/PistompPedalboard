// effects/tremolo.h -- amplitude modulation (the swampy volume pulse).
//
// An LFO multiplies the signal level, swinging it between full and (1 - depth):
// the classic surf/worship "throb". Sine shape is the smooth Fender-amp voice;
// Square is a hard chop. The gain is one-pole smoothed so the square's edges
// don't click. LFO is mono-linked across L/R (no stereo phase offset) so it reads
// as a single rhythmic pulse rather than auto-pan.

#pragma once

#include "../effect.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {

class Tremolo : public Effect {
public:
  Tremolo() : Effect("tremolo", "Tremolo") {
    rate_  = addParam("rate",  "Rate",  "Hz", 0.1f, 12.0f, 5.0f);
    depth_ = addParam("depth", "Depth", "%",  0, 100, 50);
    shape_ = addParam("shape", "Shape", "",   0, 1,   0);  // 0 sine, 1 square
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    phase_ = 0.0f;
    gain_ = 1.0f;
  }

  void process(float* L, float* R, int n) noexcept override {
    const float depth = depth_->get() / 100.0f;
    const bool square = shape_->get() > 0.5f;
    const float inc = rate_->get() / (float)sr_;
    const float smooth = square ? 0.02f : 1.0f;  // tame square edges only

    for (int i = 0; i < n; i++) {
      // Unipolar LFO in [0,1]: 1 = full volume, 0 = quietest point.
      float uni = square ? (phase_ < 0.5f ? 1.0f : 0.0f)
                         : 0.5f * (1.0f + std::cos(2.0f * (float)M_PI * phase_));
      float target = 1.0f - depth * (1.0f - uni);
      gain_ += (target - gain_) * smooth;
      L[i] *= gain_;
      R[i] *= gain_;

      phase_ += inc;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
    }
  }

private:
  double sr_ = 48000.0;
  float phase_ = 0.0f, gain_ = 1.0f;
  Param *rate_, *depth_, *shape_;
};

}  // namespace fx

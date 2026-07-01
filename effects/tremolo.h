// effects/tremolo.h -- amplitude modulation (the swampy volume pulse).
//
// An LFO multiplies the signal level, swinging it between full and (1 - depth):
// the classic surf/worship "throb". Sine shape is the smooth Fender-amp voice;
// Square is a hard chop. The gain is one-pole smoothed so the square's edges
// don't click. LFO is mono-linked across L/R (no stereo phase offset) so it reads
// as a single rhythmic pulse rather than auto-pan.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"

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
    // Beat sync: when on, Rate is ignored and the LFO period tracks Div at the
    // board tempo. Default Div = "1/8" (index 6 in tempo::kDivisions).
    sync_  = addParam("sync",  "Sync",  "",   0, 1, 0);              // toggle
    div_   = addParam("div",   "Div",   "",   0, tempo::kDivCount - 1, 6);  // enum
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    phase_ = 0.0f;
    gain_ = 1.0f;
    depthS_.prepare(sr, 20.0);
    depthS_.snap(depth_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float depthT = depth_->get() / 100.0f;
    const bool square = shape_->get() > 0.5f;
    // When synced, the LFO period IS one division at the board tempo, so the
    // rate is its reciprocal (ms -> Hz). Otherwise the manual Rate knob.
    const float rateHz =
        sync_->get() > 0.5f
            ? 1000.0f / tempo::divisionMs((int)div_->get(), tempo::bpm())
            : rate_->get();
    const float inc = rateHz / (float)sr_;
    const float smooth = square ? 0.02f : 1.0f;  // tame square edges only

    for (int i = 0; i < n; i++) {
      // Depth smoothed per sample so a swept knob glides instead of stepping
      // the throb's floor per block.
      const float depth = depthS_.next(depthT);
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
  Smoother depthS_;
  Param *rate_, *depth_, *shape_, *sync_, *div_;
};

}  // namespace fx

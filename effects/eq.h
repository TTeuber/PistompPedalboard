// effects/eq.h -- a 3-band tone shaper, right after the amp.
//
// Sits post-amp to dial the cab/amp voicing into the mix: roll off boom, scoop or
// push the mids, add air on top. Three simple ±12 dB knobs at fixed musical
// frequencies (a low shelf, a mid bell, a high shelf) -- enough to fit a guitar
// in a worship mix without an engineering degree.
//
// Built from the in-place RBJ biquads in dsp_util.h (one set per channel), so
// coefficients recompute on the audio thread with zero allocation when a knob
// moves. Flat (all 0 dB) is a true bypass: the shelf/bell math collapses to unity.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

namespace fx {

class EQ : public Effect {
public:
  EQ() : Effect("eq", "EQ") {
    low_  = addParam("low",  "Low",  "dB", -12, 12, 0);
    mid_  = addParam("mid",  "Mid",  "dB", -12, 12, 0);
    high_ = addParam("high", "High", "dB", -12, 12, 0);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    lowL_.reset();  lowR_.reset();
    midL_.reset();  midR_.reset();
    highL_.reset(); highR_.reset();
    lgS_ = low_->get(); mgS_ = mid_->get(); hgS_ = high_->get();
  }

  void process(float* L, float* R, int n) noexcept override {
    // Smooth the dB gains at BLOCK rate (~50 ms time constant): recomputing
    // biquad coefficients per sample is needless work, but easing the per-block
    // coefficient jumps down to fractions of a dB kills the zipper of a swept
    // knob just as well.
    const double a = std::exp(-(double)n / (0.050 * sr_));
    lgS_ = low_->get()  + a * (lgS_ - low_->get());
    mgS_ = mid_->get()  + a * (mgS_ - mid_->get());
    hgS_ = high_->get() + a * (hgS_ - high_->get());
    const double lg = lgS_, mg = mgS_, hg = hgS_;
    lowL_.setLowShelf(kLowHz, sr_, lg);    lowR_.setLowShelf(kLowHz, sr_, lg);
    midL_.setPeak(kMidHz, sr_, mg);        midR_.setPeak(kMidHz, sr_, mg);
    highL_.setHighShelf(kHighHz, sr_, hg); highR_.setHighShelf(kHighHz, sr_, hg);

    for (int i = 0; i < n; i++) {
      L[i] = highL_.process(midL_.process(lowL_.process(L[i])));
      R[i] = highR_.process(midR_.process(lowR_.process(R[i])));
    }
  }

private:
  static constexpr double kLowHz = 120.0, kMidHz = 800.0, kHighHz = 3200.0;
  double sr_ = 48000.0;
  Biquad lowL_, lowR_, midL_, midR_, highL_, highR_;
  double lgS_ = 0.0, mgS_ = 0.0, hgS_ = 0.0;  // block-smoothed band gains (dB)
  Param *low_, *mid_, *high_;
};

}  // namespace fx

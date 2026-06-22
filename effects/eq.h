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
  }

  void process(float* L, float* R, int n) noexcept override {
    const double lg = low_->get(), mg = mid_->get(), hg = high_->get();
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
  Param *low_, *mid_, *high_;
};

}  // namespace fx

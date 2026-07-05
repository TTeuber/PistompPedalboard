// effects/detune.h -- near-unity pitch shift, the "rack detune" studio widener.
//
// pit::PitchShift (the shimmer/octave engine) at a few-cents ratio, blended
// with the dry: the unison+detune trick that reads as instant record sheen.
// Spread on runs L down / R up (symmetric double-tracking width); off shifts
// both sides up together, which stays clean when the rig is summed to mono.
//
// The grain window is 25 ms -- far shorter than the octave's 40/70 ms. Near-
// unity ratios drift the grain phase very slowly, so the small window is
// warble-free and keeps the wet-only latency to a ~5..25 ms sweep (~12 ms
// average). Post-amp at 50% mix that reads as thickness, not smear.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "pitch_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace fx {

class Detune : public Effect {
public:
  Detune() : Effect("detune", "Detune") {
    cents_  = addParam("cents",  "Cents",  "cents", 0, 50, 10);
    spread_ = addParam("spread", "Spread", "",      0, 1,  1);  // toggle
    mix_    = addParam("mix",    "Mix",    "%",     0, 100, 50);
  }

  void prepare(double sr, int) override {
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    psL_.prepare(mono, kWindowMs, sr);
    psR_.prepare(mono, kWindowMs, sr);
    centsS_.prepare(sr, 20.0);
    centsS_.snap(cents_->get());
    // L's shift direction glides through zero on a Spread toggle, so the
    // switch is a slow retune rather than a click.
    sgnS_.prepare(sr, 50.0);
    sgnS_.snap(spread_->get() > 0.5f ? -1.0f : 1.0f);
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float centsT = cents_->get();
    const float sgnT = spread_->get() > 0.5f ? -1.0f : 1.0f;
    const float mixT = mix_->get() / 100.0f;

    for (int i = 0; i < n; i++) {
      // Ratio changes only alter the grain tap speed (inherently click-free),
      // but smoothing cents/sign makes knob sweeps read as a tape-style bend.
      const float c = centsS_.next(centsT);
      const float sgn = sgnS_.next(sgnT);
      const float mix = mixS_.next(mixT);
      psL_.setRatio(std::exp2(sgn * c / 1200.0f));
      psR_.setRatio(std::exp2(c / 1200.0f));
      const float inL = L[i], inR = R[i];
      L[i] = inL * (1.0f - mix) + psL_.process(inL) * mix;
      R[i] = inR * (1.0f - mix) + psR_.process(inR) * mix;
    }
  }

private:
  static constexpr float kWindowMs = 25.0f;

  pit::PitchShift psL_, psR_;
  Smoother centsS_, sgnS_, mixS_;
  Param *cents_, *spread_, *mix_;
};

}  // namespace fx

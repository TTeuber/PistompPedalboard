// effects/vibrato.h -- pure pitch-wobble modulation (split out of Chorus).
//
// A 100%-wet swept delay: you hear only the pitch wobble, no dry blend -- a true
// vibrato. Hand-written on a juce::dsp::DelayLine so Depth can be a real musical
// quantity (CENTS of peak pitch deviation) instead of an opaque 0..100%.
//
// The pitch deviation of a sinusoidally-swept delay is ratio r = 2π·f·A, where f
// is the LFO rate and A the delay-sweep amplitude (seconds). So for a target of C
// cents we solve A = (2^(C/1200) - 1) / (2π·f): faster wobble needs a smaller
// sweep for the same depth. A is capped so the delay buffer stays bounded (at
// very slow rates the achieved depth eases below the dial, which is inaudible
// there anyway). The two channels run the LFO a quarter-cycle apart for width.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {

class Vibrato : public Effect {
public:
  Vibrato() : Effect("vibrato", "Vibrato") {
    rate_  = addParam("rate",  "Rate",  "Hz",    0.1f, 8.0f, 5.0f);
    depth_ = addParam("depth", "Depth", "cents", 0,    50,   15);
  }

  void prepare(double sr, int maxBlock) override {
    sr_ = sr;
    centreSamps_ = (float)(sr * 0.012);   // 12 ms nominal read point
    maxModSamps_ = (float)(sr * 0.010);   // sweep capped at ±10 ms
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    line_.prepare(spec);
    line_.setMaximumDelayInSamples((int)std::ceil(sr * 0.030) + 8);
    line_.reset();
    phase_ = 0.0f;
    aSampS_.prepare(sr, 20.0);
    aSampS_.snap(sweepTarget());
  }

  void process(float* L, float* R, int n) noexcept override {
    const float f = rate_->get();
    const float aTarget = sweepTarget();
    const float inc = f / (float)sr_;

    for (int i = 0; i < n; i++) {
      // Smoothed per sample: Depth/Rate scale the read tap, so a block-rate
      // step there jumps the tap position -- an audible click.
      const float aSamp = aSampS_.next(aTarget);
      float lfoL = std::sin(2.0f * (float)M_PI * phase_);
      float lfoR = std::sin(2.0f * (float)M_PI * (phase_ + 0.25f));
      float inL = L[i], inR = R[i];
      float wetL = line_.popSample(0, centreSamps_ + aSamp * lfoL);
      float wetR = line_.popSample(1, centreSamps_ + aSamp * lfoR);
      line_.pushSample(0, inL);
      line_.pushSample(1, inR);
      L[i] = wetL;  // fully wet: pure pitch wobble
      R[i] = wetR;

      phase_ += inc;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
    }
  }

private:
  // Sweep amplitude (samples) that yields Depth cents of peak deviation at the
  // current Rate (see the header comment for the derivation), capped so the
  // read tap stays inside the buffer.
  float sweepTarget() const noexcept {
    const float f = rate_->get();
    const float r = std::pow(2.0f, depth_->get() / 1200.0f) - 1.0f;
    const float a =
        (f > 0.01f) ? r / (2.0f * (float)M_PI * f) * (float)sr_ : 0.0f;
    return std::min(a, maxModSamps_);
  }

  double sr_ = 48000.0;
  float centreSamps_ = 576.0f, maxModSamps_ = 480.0f;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  float phase_ = 0.0f;
  Smoother aSampS_;
  Param *rate_, *depth_;
};

}  // namespace fx

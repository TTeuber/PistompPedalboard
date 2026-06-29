// effects/flanger.h -- short swept comb-filter "jet plane" modulation.
//
// A flanger is a chorus with a much SHORTER delay (sub-millisecond up to a few
// ms) and a resonant feedback path -- the swept comb notches sweep through the
// harmonics for that whooshing jet sound. Built by hand on a juce::dsp::DelayLine
// (Lagrange interp for clean fractional delays at these tiny times) plus a
// per-sample LFO; the two channels run the LFO 90 degrees apart for stereo width.

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {

class Flanger : public Effect {
public:
  Flanger() : Effect("flanger", "Flanger") {
    rate_  = addParam("rate",     "Rate",     "Hz", 0.05f, 5.0f, 0.3f);
    depth_ = addParam("depth",    "Depth",    "%",  0, 100, 70);
    fb_    = addParam("feedback", "Feedback", "%",  0, 95,  50);
    mix_   = addParam("mix",      "Mix",      "%",  0, 100, 50);
  }

  void prepare(double sr, int maxBlock) override {
    sr_ = sr;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    line_.prepare(spec);
    line_.setMaximumDelayInSamples((int)std::ceil(sr * (kMaxMs / 1000.0)) + 8);
    line_.reset();
    phase_ = 0.0f;
  }

  void process(float* L, float* R, int n) noexcept override {
    const float depth = depth_->get() / 100.0f;
    const float fb = fb_->get() / 100.0f;
    const float mix = mix_->get() / 100.0f;
    const float inc = rate_->get() / (float)sr_;
    // Sweep from ~0.5 ms out to ~0.5 + depth*5 ms.
    const float baseSamps = 0.0005f * (float)sr_;
    const float sweepSamps = depth * 0.005f * (float)sr_;

    for (int i = 0; i < n; i++) {
      // Raised-cosine LFO (0..1); R tapped a quarter cycle ahead for width.
      float lfoL = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * phase_));
      float lfoR =
          0.5f * (1.0f - std::cos(2.0f * (float)M_PI * (phase_ + 0.25f)));
      float dL = baseSamps + sweepSamps * lfoL;
      float dR = baseSamps + sweepSamps * lfoR;

      float inL = L[i], inR = R[i];
      float wetL = line_.popSample(0, dL);
      float wetR = line_.popSample(1, dR);
      line_.pushSample(0, inL + wetL * fb);
      line_.pushSample(1, inR + wetR * fb);

      L[i] = inL * (1.0f - mix) + wetL * mix;
      R[i] = inR * (1.0f - mix) + wetR * mix;

      phase_ += inc;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
    }
  }

private:
  static constexpr float kMaxMs = 10.0f;
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  float phase_ = 0.0f;
  Param *rate_, *depth_, *fb_, *mix_;
};

}  // namespace fx

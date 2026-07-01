// effects/reverb.h -- ambient reverb, the heart of modern worship tone.
//
// Wraps juce::Reverb (the Freeverb model in juce_audio_basics), whose
// processStereo() is already in-place stereo -- a perfect fit for our Effect
// interface, no AudioBlock plumbing needed. "Freeze" holds an infinite wash,
// great for swells under a pad.

#pragma once

#include "../effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace fx {

class Reverb : public Effect {
public:
  Reverb() : Effect("reverb", "Reverb") {
    size_   = addParam("size",    "Size",    "%", 0, 100, 65);
    damp_   = addParam("damping", "Damping", "%", 0, 100, 40);
    width_  = addParam("width",   "Width",   "%", 0, 100, 100);
    mix_    = addParam("mix",     "Mix",     "%", 0, 100, 30);
    freeze_ = addParam("freeze",  "Freeze",  "",  0, 1,   0);
  }

  // Bypass lets the wash ring out (the chain keeps running us with the input
  // faded to silence) instead of cutting it off mid-decay. Note: with Freeze
  // held the wash sustains right through bypass -- by design.
  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int) override { rev_.setSampleRate(sr); rev_.reset(); }

  // No parameter smoothing needed here: juce::Reverb smooths damping, feedback
  // and the wet/dry gains internally, so block-rate setParameters() is fine.
  void process(float* L, float* R, int n) noexcept override {
    juce::Reverb::Parameters p;
    const float mix = mix_->get() / 100.0f;
    p.roomSize   = size_->get() / 100.0f;
    p.damping    = damp_->get() / 100.0f;
    p.width      = width_->get() / 100.0f;
    p.wetLevel   = mix;
    p.dryLevel   = 1.0f - mix;
    p.freezeMode = freeze_->get() > 0.5f ? 1.0f : 0.0f;
    rev_.setParameters(p);
    rev_.processStereo(L, R, n);
  }

private:
  juce::Reverb rev_;
  Param *size_, *damp_, *width_, *mix_, *freeze_;
};

}  // namespace fx

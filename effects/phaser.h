// effects/phaser.h -- sweeping notch-comb modulation, post-amp.
//
// Wraps juce::dsp::Phaser (a chain of all-pass stages swept by an LFO). The
// moving notches give that watery, rotating sweep -- a worship-clean staple,
// subtler and more vocal than the chorus's detune. Genuine stereo.

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

namespace fx {

class Phaser : public Effect {
public:
  Phaser() : Effect("phaser", "Phaser") {
    rate_  = addParam("rate",     "Rate",     "Hz", 0.05f, 8.0f, 0.5f);
    depth_ = addParam("depth",    "Depth",    "%",  0, 100, 50);
    fb_    = addParam("feedback", "Feedback", "%",  0, 100, 30);
    mix_   = addParam("mix",      "Mix",      "%",  0, 100, 50);
  }

  void prepare(double sr, int maxBlock) override {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    phaser_.prepare(spec);
    phaser_.reset();
  }

  // No parameter smoothing needed here: juce::dsp::Phaser smooths its rate/
  // depth/mix/feedback internally, so block-rate setters don't zipper.
  void process(float* L, float* R, int n) noexcept override {
    phaser_.setRate(rate_->get());
    phaser_.setDepth(depth_->get() / 100.0f);
    phaser_.setFeedback(fb_->get() / 100.0f * 0.9f);  // keep stable (<1.0)
    phaser_.setCentreFrequency(600.0f);                // sweep around ~600 Hz
    phaser_.setMix(mix_->get() / 100.0f);

    float* channels[2] = {L, R};
    juce::dsp::AudioBlock<float> block(channels, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    phaser_.process(ctx);
  }

private:
  juce::dsp::Phaser<float> phaser_;
  Param *rate_, *depth_, *fb_, *mix_;
};

}  // namespace fx

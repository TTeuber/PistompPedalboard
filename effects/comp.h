// effects/comp.h -- an easy compressor, right after the gate.
//
// Compression evens out picking dynamics so quiet notes come up and loud ones
// don't jump -- the glue under clean worship parts and the sustain under leads.
// Pro comps bury students in threshold/ratio/attack/release/knee; here it's ONE
// "Amount" knob (threshold + ratio move together, gentle->squeezed) plus a
// "Makeup" gain to bring the level back up. Wraps juce::dsp::Compressor, whose
// process() is allocation-free; makeup is a plain post-gain.

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace fx {

class Comp : public Effect {
public:
  Comp() : Effect("comp", "Compressor") {
    amount_ = addParam("amount", "Amount", "%",  0, 100, 40);
    makeup_ = addParam("makeup", "Makeup", "dB", 0, 24,  6);
  }

  void prepare(double sr, int maxBlock) override {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    comp_.prepare(spec);
    comp_.reset();
  }

  void process(float* L, float* R, int n) noexcept override {
    const float amt = amount_->get() / 100.0f;          // 0..1
    comp_.setThreshold(-amt * 40.0f);                    // 0 dB .. -40 dB
    comp_.setRatio(1.5f + amt * 6.5f);                   // 1.5:1 .. 8:1
    comp_.setAttack(15.0f);                              // musical, fixed
    comp_.setRelease(150.0f);
    const float makeup = std::pow(10.0f, makeup_->get() / 20.0f);

    float* channels[2] = {L, R};
    juce::dsp::AudioBlock<float> block(channels, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    comp_.process(ctx);

    for (int i = 0; i < n; i++) { L[i] *= makeup; R[i] *= makeup; }
  }

private:
  juce::dsp::Compressor<float> comp_;
  Param *amount_, *makeup_;
};

}  // namespace fx

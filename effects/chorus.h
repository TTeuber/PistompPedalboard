// effects/chorus.h -- chorus modulation, post-amp.
//
// Wraps juce::dsp::Chorus (a modulated delay). Chorus = wet + dry mixed, giving
// that shimmering, slightly-detuned width worship clean tones love. Genuine
// stereo: it spreads the modulation across L/R, opening up the mono amp signal.
// (The 100%-wet "vibrato" voice now lives in its own pedal -- see vibrato.h.)

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

namespace fx {

class Chorus : public Effect {
public:
  Chorus() : Effect("chorus", "Chorus") {
    rate_  = addParam("rate",     "Rate",     "Hz", 0.05f, 8.0f, 0.8f);
    depth_ = addParam("depth",    "Depth",    "%",  0, 100, 30);
    fb_    = addParam("feedback", "Feedback", "%",  0, 100, 10);
    mix_   = addParam("mix",      "Mix",      "%",  0, 100, 35);
  }

  void prepare(double sr, int maxBlock) override {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    chorus_.prepare(spec);
    chorus_.reset();
  }

  void process(float* L, float* R, int n) noexcept override {
    chorus_.setRate(rate_->get());
    chorus_.setDepth(depth_->get() / 100.0f);
    chorus_.setFeedback(fb_->get() / 100.0f * 0.9f);  // keep stable (<1.0)
    chorus_.setCentreDelay(7.0f);                      // classic ~7 ms voice
    chorus_.setMix(mix_->get() / 100.0f);

    float* channels[2] = {L, R};
    juce::dsp::AudioBlock<float> block(channels, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    chorus_.process(ctx);
  }

private:
  juce::dsp::Chorus<float> chorus_;
  Param *rate_, *depth_, *fb_, *mix_;
};

}  // namespace fx

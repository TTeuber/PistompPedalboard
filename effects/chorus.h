// effects/chorus.h -- chorus / vibrato modulation, post-amp.
//
// Wraps juce::dsp::Chorus (a modulated delay). Chorus = wet + dry mixed, giving
// that shimmering, slightly-detuned width worship clean tones love. Flip the
// Vibrato toggle and it goes 100% wet, so you hear only the pitch wobble of the
// modulated delay -- a true vibrato. Genuine stereo: it spreads the modulation
// across L/R, opening up the mono amp signal.

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class Chorus : public Effect {
public:
  Chorus() : Effect("chorus", "Chorus") {
    rate_    = addParam("rate",     "Rate",     "Hz", 0.05f, 8.0f, 0.8f);
    depth_   = addParam("depth",    "Depth",    "%",  0, 100, 30);
    fb_      = addParam("feedback", "Feedback", "%",  0, 100, 10);
    mix_     = addParam("mix",      "Mix",      "%",  0, 100, 35);
    vibrato_ = addParam("vibrato",  "Vibrato",  "",   0, 1,   0);  // toggle
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
    const bool vib = vibrato_->get() > 0.5f;
    chorus_.setRate(rate_->get());
    chorus_.setDepth(depth_->get() / 100.0f);
    chorus_.setFeedback(fb_->get() / 100.0f * 0.9f);   // keep stable (<1.0)
    chorus_.setCentreDelay(7.0f);                       // classic ~7 ms voice
    chorus_.setMix(vib ? 1.0f : mix_->get() / 100.0f);  // vibrato = fully wet

    float* channels[2] = {L, R};
    juce::dsp::AudioBlock<float> block(channels, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    chorus_.process(ctx);
  }

private:
  juce::dsp::Chorus<float> chorus_;
  Param *rate_, *depth_, *fb_, *mix_, *vibrato_;
};

}  // namespace fx

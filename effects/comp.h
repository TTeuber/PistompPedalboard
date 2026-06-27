// effects/comp.h -- an easy compressor, right after the gate.
//
// Compression evens out picking dynamics so quiet notes come up and loud ones
// don't jump -- the glue under clean worship parts and the sustain under leads.
// Pro comps bury students in attack/release/knee; here it's the two knobs that
// actually teach the concept -- "Threshold" (how loud before it clamps) and
// "Ratio" (how hard) -- plus a "Makeup" gain to bring the level back up. Attack/
// release stay fixed and musical. Wraps juce::dsp::Compressor, whose process() is
// allocation-free; makeup is a plain post-gain.
//
// The web UI draws the threshold as a marker line on the input meter and a shared
// gain-reduction bar, so we measure how much the compressor pulled the block down
// (peak in vs. peak out, before makeup) and publish it lock-free in grDb_.

#pragma once

#include "../effect.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace fx {

class Comp : public Effect {
public:
  Comp() : Effect("comp", "Compressor") {
    thresh_ = addParam("threshold", "Threshold", "dB", -40, 0, -18);
    ratio_  = addParam("ratio",     "Ratio",     ":1", 1,  10, 4);
    makeup_ = addParam("makeup",    "Makeup",    "dB", 0,  24, 6);
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
    comp_.setThreshold(thresh_->get());
    comp_.setRatio(ratio_->get());
    comp_.setAttack(15.0f);                              // musical, fixed
    comp_.setRelease(150.0f);
    const float makeup = std::pow(10.0f, makeup_->get() / 20.0f);

    // Block peak going in, so we can report how much the comp clamped (the
    // gain-reduction meter). Pre-makeup, so makeup gain doesn't mask it.
    float pkIn = 0.0f;
    for (int i = 0; i < n; i++)
      pkIn = std::max(pkIn, std::max(std::fabs(L[i]), std::fabs(R[i])));

    float* channels[2] = {L, R};
    juce::dsp::AudioBlock<float> block(channels, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    comp_.process(ctx);

    float pkOut = 0.0f;
    for (int i = 0; i < n; i++)
      pkOut = std::max(pkOut, std::max(std::fabs(L[i]), std::fabs(R[i])));

    // Gain reduction in dB (>= 0), only meaningful when there was signal to clamp.
    float gr = (pkIn > 1e-6f && pkOut < pkIn)
                   ? 20.0f * std::log10(pkIn / pkOut)
                   : 0.0f;
    gr = std::clamp(gr, 0.0f, 24.0f);
    // Peak-hold: keep the largest reduction since the meter last read-and-cleared
    // (same lock-free max as PedalControls::inPeak). A disabled comp never writes,
    // so its held value decays to 0 on the next web read.
    float cur = grDb_.load(std::memory_order_relaxed);
    while (gr > cur &&
           !grDb_.compare_exchange_weak(cur, gr, std::memory_order_relaxed)) {}

    for (int i = 0; i < n; i++) { L[i] *= makeup; R[i] *= makeup; }
  }

  // Largest gain reduction (dB) since the last call; reading clears the peak hold
  // so a disabled comp decays to 0. Called by the web meter (web_server.cpp).
  float takeGrDb() noexcept { return grDb_.exchange(0.0f, std::memory_order_relaxed); }

private:
  juce::dsp::Compressor<float> comp_;
  Param *thresh_, *ratio_, *makeup_;
  std::atomic<float> grDb_{0.0f};
};

}  // namespace fx

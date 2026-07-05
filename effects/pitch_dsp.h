// effects/pitch_dsp.h -- the shared granular pitch shifter.
//
// Extracted verbatim from reverb_dsp.h once it grew a third consumer (the
// FX_NOTES rule): shimmer_reverb.h (octave in the feedback wash), octave.h
// (Poly mode's chord-friendly taps), detune.h (near-unity rack detune).
// Allocation-free after prepare(), lock-free per sample; not an Effect.

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace pit {

// Granular pitch shifter -- two delay taps a half-window apart, crossfaded with
// Hann envelopes (which sum to unity at that offset) so the moving read pointer
// never clicks as it wraps. Window size trades smear for warble: long windows
// (~70 ms) ride out big ratios, short windows (~25 ms) keep near-unity ratios
// tight and low-latency.
struct PitchShift {
  void prepare(const juce::dsp::ProcessSpec& spec, float windowMs, double fs) {
    fs_ = fs;
    window_ = windowMs * 0.001f * (float)fs;
    line_.prepare(spec);
    line_.setMaximumDelayInSamples((int)std::ceil(window_) + 8);
    line_.reset();
    phase_ = 0.0f;
  }
  void reset() noexcept { line_.reset(); phase_ = 0.0f; }
  void setRatio(float r) noexcept { ratio_ = r; }

  float process(float x) noexcept {
    line_.pushSample(0, x);
    float p2 = phase_ + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    // Keep a 1-sample floor so the read tap never touches the write pointer.
    const float d1 = 1.0f + phase_ * (window_ - 2.0f);
    const float d2 = 1.0f + p2 * (window_ - 2.0f);
    const float g1 = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * phase_));
    const float g2 = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * p2));
    const float y = g1 * line_.popSample(0, d1) + g2 * line_.popSample(0, d2);
    // Pitch up (ratio>1) means the read must outrun the write, i.e. the delay
    // shrinks -> phase decreases; wrap keeps it in [0,1).
    phase_ -= (ratio_ - 1.0f) / window_;
    while (phase_ >= 1.0f) phase_ -= 1.0f;
    while (phase_ < 0.0f) phase_ += 1.0f;
    return y;
  }

private:
  double fs_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  float window_ = 0.0f, phase_ = 0.0f, ratio_ = 2.0f;
};

}  // namespace pit
}  // namespace fx

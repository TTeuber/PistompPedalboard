// effects/delay.h -- stereo echo with feedback, tone, and ping-pong.
//
// A juce::dsp::DelayLine per channel for the delay buffer; the feedback path is
// done by hand so we can darken each repeat (a one-pole lowpass in the loop, the
// "Tone" knob) -- analog-style echoes that fade away instead of piling up harsh.
// Ping-pong crosses the feedback L<->R so repeats bounce across the stereo field,
// a staple of ambient worship leads.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class Delay : public Effect {
public:
  Delay() : Effect("delay", "Delay") {
    time_     = addParam("time",     "Time",      "ms", 20, 1000, 380);
    fb_       = addParam("feedback", "Feedback",  "%",  0,  95,   35);
    mix_      = addParam("mix",      "Mix",       "%",  0,  100,  30);
    tone_     = addParam("tone",     "Tone",      "%",  0,  100,  55);
    pingpong_ = addParam("pingpong", "Ping-Pong", "",   0,  1,    0);  // toggle
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
    toneL_.reset();
    toneR_.reset();
  }

  void process(float* L, float* R, int n) noexcept override {
    const float delaySamps =
        std::clamp(time_->get(), 1.0f, kMaxMs) * 0.001f * (float)sr_;
    const float fb = fb_->get() / 100.0f;
    const float mix = mix_->get() / 100.0f;
    const bool ping = pingpong_->get() > 0.5f;
    // Tone: darken the repeats from ~1.2 kHz (dark) up to ~12 kHz (bright).
    const double fc = 1200.0 * std::pow(10.0, tone_->get() / 100.0);
    toneL_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    toneR_.setCutoff(std::min(fc, sr_ * 0.45), sr_);

    line_.setDelay(delaySamps);
    for (int i = 0; i < n; i++) {
      float inL = L[i], inR = R[i];
      float dL = line_.popSample(0);
      float dR = line_.popSample(1);
      // Darken the feedback signal so each echo loses highs (analog feel).
      float fL = toneL_.process(dL) * fb;
      float fR = toneR_.process(dR) * fb;
      if (ping) {  // cross the feedback so echoes bounce L<->R
        line_.pushSample(0, inL + fR);
        line_.pushSample(1, inR + fL);
      } else {
        line_.pushSample(0, inL + fL);
        line_.pushSample(1, inR + fR);
      }
      L[i] = inL * (1.0f - mix) + dL * mix;
      R[i] = inR * (1.0f - mix) + dR * mix;
    }
  }

private:
  static constexpr float kMaxMs = 1000.0f;
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line_;
  OnePole toneL_, toneR_;
  Param *time_, *fb_, *mix_, *tone_, *pingpong_;
};

}  // namespace fx

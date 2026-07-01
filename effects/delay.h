// effects/delay.h -- stereo echo with feedback, tone, and ping-pong.
//
// A juce::dsp::DelayLine per channel for the delay buffer; the feedback path is
// done by hand so we can darken each repeat (a one-pole lowpass in the loop, the
// "Tone" knob) -- analog-style echoes that fade away instead of piling up harsh.
// Ping-pong crosses the feedback L<->R so repeats bounce across the stereo field,
// a staple of ambient worship leads.
//
// Time changes GLIDE (a rate-capped one-pole on the delay length) instead of
// jumping the read tap: sweeping the knob or changing Sync/Div gives the tape-
// machine pitch bend, never a click. Bypass keeps the tail: hasTails() tells the
// chain to keep running us with the input faded out so repeats decay naturally.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"

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
    // Beat sync: when on, Time is ignored and the delay tracks Div at the board
    // tempo (tempo::bpm()). Default Div = "1/8" (index 6 in tempo::kDivisions).
    sync_     = addParam("sync",     "Sync",      "",   0, 1, 0);              // toggle
    div_      = addParam("div",      "Div",       "",   0, tempo::kDivCount - 1, 6);  // enum
  }

  bool hasTails() const noexcept override { return true; }

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
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    timeK_ = float(1.0 - std::exp(-1.0 / (0.100 * sr)));  // ~100 ms glide
    timeSm_ = targetSamps();
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const bool ping = pingpong_->get() > 0.5f;
    // Tone: darken the repeats from ~1.2 kHz (dark) up to ~12 kHz (bright).
    const double fc = 1200.0 * std::pow(10.0, tone_->get() / 100.0);
    toneL_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    toneR_.setCutoff(std::min(fc, sr_ * 0.45), sr_);

    for (int i = 0; i < n; i++) {
      // Glide the delay length toward the target. The one-pole eases small
      // wiggles; the +/-kMaxSlew cap turns big jumps (Sync/Div changes, knob
      // sweeps) into a bounded tape-style pitch bend, and keeps the read tap
      // from ever outrunning the write head.
      timeSm_ += std::clamp((dTarget - timeSm_) * timeK_, -kMaxSlew, kMaxSlew);
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);

      float inL = L[i], inR = R[i];
      float dL = line_.popSample(0, timeSm_);
      float dR = line_.popSample(1, timeSm_);
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
  // Current Time target in samples: the synced division or the manual knob.
  float targetSamps() const noexcept {
    const float timeMs = sync_->get() > 0.5f
                             ? tempo::divisionMs((int)div_->get(), tempo::bpm())
                             : time_->get();
    return std::clamp(timeMs, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 1000.0f;
  static constexpr float kMaxSlew = 0.5f;  // max delay change, samples/sample
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line_;
  OnePole toneL_, toneR_;
  Smoother fbS_, mixS_;
  float timeSm_ = 0.0f, timeK_ = 0.0f;  // glided delay length + glide coeff
  Param *time_, *fb_, *mix_, *tone_, *pingpong_, *sync_, *div_;
};

}  // namespace fx

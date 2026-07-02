// effects/delay.h -- Digital: pristine stereo echo with feedback, tone,
// ping-pong, stereo spread, and freeze. The bread-and-butter of the delay
// collection (see delay_dsp.h for the shared pieces and the freeze recipe).
//
// A juce::dsp::DelayLine per channel for the delay buffer; the feedback path is
// done by hand so we can shape each repeat: a one-pole lowpass (the "Tone"
// knob) darkens the echoes, and a fixed 100 Hz highpass sheds lows so long
// trails never pile up into mud. Ping-pong crosses the feedback L<->R so
// repeats bounce across the stereo field; Spread runs the right channel up to
// 10% longer than the left so straight repeats bloom wide instead of stacking
// center.
//
// Time changes GLIDE (dly::GlideValue, the rate-capped one-pole) instead of
// jumping the read tap: sweeping the knob or changing Sync/Div gives the tape-
// machine pitch bend, never a click. Freeze locks the loop at exactly unity
// with the input muted and the loop filters faded out -- the held repeats
// circulate losslessly (and keep bouncing if ping-pong is on). Bypass keeps
// the tail: hasTails() tells the chain to keep running us with the input faded
// out so repeats (and a frozen loop) decay -- or hold -- naturally.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "delay_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class Delay : public Effect {
public:
  Delay() : Effect("delay", "Digital") {
    time_     = addParam("time",     "Time",      "ms", 20, 2000, 380);
    fb_       = addParam("feedback", "Feedback",  "%",  0,  95,   35);
    mix_      = addParam("mix",      "Mix",       "%",  0,  100,  30);
    tone_     = addParam("tone",     "Tone",      "%",  0,  100,  55);
    pingpong_ = addParam("pingpong", "Ping-Pong", "",   0,  1,    0);  // toggle
    // Beat sync: when on, Time is ignored and the delay tracks Div at the board
    // tempo (tempo::bpm()). Default Div = "1/8" (index 6 in tempo::kDivisions).
    sync_     = addParam("sync",     "Sync",      "",   0, 1, 0);              // toggle
    div_      = addParam("div",      "Div",       "",   0, tempo::kDivCount - 1, 6);  // enum
    spread_   = addParam("spread",   "Spread",    "%",  0,  100,  0);
    freeze_   = addParam("freeze",   "Freeze",    "",   0,  1,    0);  // toggle
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
    hpL_.setCutoff(100.0, sr);
    hpR_.setCutoff(100.0, sr);
    hpL_.reset();
    hpR_.reset();
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    frz_.prepare(sr);
    glideL_.prepare(sr);  // ~100 ms glide, 0.5 samples/sample slew
    glideR_.prepare(sr);
    glideL_.snap(targetSampsL());
    glideR_.snap(targetSampsR());
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTargetL = targetSampsL();
    const float dTargetR = targetSampsR();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const bool ping = pingpong_->get() > 0.5f;
    const bool frozen = freeze_->get() > 0.5f;
    // Tone: darken the repeats from ~1.2 kHz (dark) up to ~12 kHz (bright).
    const double fc = 1200.0 * std::pow(10.0, tone_->get() / 100.0);
    toneL_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    toneR_.setCutoff(std::min(fc, sr_ * 0.45), sr_);

    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float frz = frz_.next(frozen);

      float inL = L[i], inR = R[i];
      float dL = line_.popSample(0, glideL_.next(dTargetL));
      float dR = line_.popSample(1, glideR_.next(dTargetR));
      // Loop coloring (tone lowpass + 100 Hz highpass) crossfades out as the
      // freeze engages, so the frozen loop circulates losslessly at unity.
      const float cL = hpL_.process(toneL_.process(dL));
      const float cR = hpR_.process(toneR_.process(dR));
      const float loopGain = fb * (1.0f - frz) + frz;
      const float fL = (cL * (1.0f - frz) + dL * frz) * loopGain;
      const float fR = (cR * (1.0f - frz) + dR * frz) * loopGain;
      const float jL = inL * (1.0f - frz);  // input injection mutes when frozen
      const float jR = inR * (1.0f - frz);
      if (ping) {  // cross the feedback so echoes bounce L<->R
        line_.pushSample(0, dly::softLimit(jL + fR));
        line_.pushSample(1, dly::softLimit(jR + fL));
      } else {
        line_.pushSample(0, dly::softLimit(jL + fL));
        line_.pushSample(1, dly::softLimit(jR + fR));
      }
      L[i] = inL * (1.0f - mix) + dL * mix;
      R[i] = inR * (1.0f - mix) + dR * mix;
    }
  }

private:
  // Current Time target in samples: the synced division or the manual knob.
  float targetSampsL() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get());
    return std::clamp(ms, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }
  // Spread runs the right channel up to 10% longer, still inside the buffer.
  float targetSampsR() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get()) *
                     (1.0f + 0.10f * spread_->get() / 100.0f);
    return std::clamp(ms, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 2000.0f;
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line_;
  OnePole toneL_, toneR_;
  dly::HighPass1 hpL_, hpR_;
  Smoother fbS_, mixS_;
  dly::Freeze frz_;
  dly::GlideValue glideL_, glideR_;
  Param *time_, *fb_, *mix_, *tone_, *pingpong_, *sync_, *div_, *spread_,
      *freeze_;
};

}  // namespace fx

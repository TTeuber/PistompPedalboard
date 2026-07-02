// effects/tape_echo.h -- Tape Echo: Echoplex/Space Echo-style delay with a
// moving transport. Wow (slow speed drift) and flutter (fast jitter) modulate
// the read head; the record/repro chain -- head-bump low-mid peak, top-end
// rolloff, and gentle tape saturation -- sits on the WRITE side of the loop,
// so the first repeat is already tape-colored and every pass around the loop
// accumulates more (the RE-201 behavior). One Age knob ages the whole
// machine: more bump, darker rolloff, thinner lows, hotter saturation.
//
// Feedback runs to 110%: past unity the loop self-oscillates, building until
// dly::softLimit's ceiling catches it -- it plateaus and darkens instead of
// blowing up, the classic runaway-echo performance trick. L/R share the
// glided delay time but carry independently-phased WowFlutter instances, so
// the two "tracks" drift apart for free stereo. Freeze (see delay_dsp.h)
// mutes the input, locks the loop at unity, and fades the tape color out so
// the held repeats stop degrading.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "delay_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class TapeEcho : public Effect {
public:
  TapeEcho() : Effect("tape", "Tape Echo") {
    time_    = addParam("time",     "Time",     "ms", 30, 1500, 450);
    fb_      = addParam("feedback", "Feedback", "%",  0,  110,  40);
    mix_     = addParam("mix",      "Mix",      "%",  0,  100,  35);
    wow_     = addParam("wow",      "Wow",      "%",  0,  100,  30);
    flutter_ = addParam("flutter",  "Flutter",  "%",  0,  100,  20);
    age_     = addParam("age",      "Age",      "%",  0,  100,  40);
    sync_    = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_     = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 6);  // enum
    freeze_  = addParam("freeze",   "Freeze",   "",   0, 1, 0);              // toggle
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int maxBlock) override {
    sr_ = sr;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    line_.prepare(spec);
    line_.setMaximumDelayInSamples(
        (int)std::ceil(sr * ((kMaxMs + dly::WowFlutter::kGuardMs) / 1000.0)) + 8);
    line_.reset();
    for (int c = 0; c < 2; c++) {
      bump_[c].reset();
      rolloff_[c].reset();
      hp_[c].reset();
    }
    // Different LFO phases per channel: the two tape "tracks" never move as one.
    wf_[0].prepare(sr, 0.00f, 0.00f);
    wf_[1].prepare(sr, 0.31f, 0.47f);
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    wowS_.prepare(sr, 20.0);
    fltS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    wowS_.snap(wow_->get() / 100.0f);
    fltS_.snap(flutter_->get() / 100.0f);
    frz_.prepare(sr);
    glide_.prepare(sr);  // ~100 ms glide -- the varispeed pitch bend
    glide_.snap(targetSamps());
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const float wowT = wow_->get() / 100.0f;
    const float fltT = flutter_->get() / 100.0f;
    const bool frozen = freeze_->get() > 0.5f;

    // Age the machine (block rate): head bump grows +1.5..+4 dB, the repro
    // rolloff drops 12 kHz -> 3 kHz, the highpass creeps 40 -> 150 Hz, and the
    // tape drive heats up.
    const float age = age_->get() / 100.0f;
    const double bumpDb = 1.5 + 2.5 * age;
    const double lpFc = 12000.0 * std::pow(10.0, -0.6 * age);
    const double hpFc = 40.0 + 110.0 * age;
    const float g = 1.2f + 1.0f * age;
    for (int c = 0; c < 2; c++) {
      bump_[c].setPeak(110.0, sr_, bumpDb, 0.8);
      rolloff_[c].setCutoff(std::min(lpFc, sr_ * 0.45), sr_);
      hp_[c].setCutoff(hpFc, sr_);
    }

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float wowA = wowS_.next(wowT);
      const float fltA = fltS_.next(fltT);
      const float frz = frz_.next(frozen);
      const float d = glide_.next(dTarget);
      const float loopGain = fb * (1.0f - frz) + frz;

      for (int c = 0; c < 2; c++) {
        const float in = io[c][i];
        // The read head wanders with the transport.
        const float tapPos =
            std::max(1.0f, d + wf_[c].nextMs(wowA, fltA) * 0.001f * (float)sr_);
        const float tap = line_.popSample(c, tapPos);

        // Write side: input + circulating loop, through the tape chain. The
        // color crossfades to the raw sum as freeze engages (frozen repeats
        // stop degrading); the filters keep running either way so their state
        // stays warm across the fade.
        const float x = in * (1.0f - frz) + tap * loopGain;
        float col = bump_[c].process(x);
        col = rolloff_[c].process(col);
        col = hp_[c].process(col);
        col = std::tanh(g * col) / g;
        line_.pushSample(c, dly::softLimit(col * (1.0f - frz) + x * frz));

        io[c][i] = in * (1.0f - mix) + tap * mix;
      }
    }
  }

private:
  float targetSamps() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get());
    return std::clamp(ms, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 1500.0f;
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  Biquad bump_[2];
  OnePole rolloff_[2];
  dly::HighPass1 hp_[2];
  dly::WowFlutter wf_[2];
  Smoother fbS_, mixS_, wowS_, fltS_;
  dly::Freeze frz_;
  dly::GlideValue glide_;
  Param *time_, *fb_, *mix_, *wow_, *flutter_, *age_, *sync_, *div_, *freeze_;
};

}  // namespace fx

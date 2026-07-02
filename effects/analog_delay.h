// effects/analog_delay.h -- Analog: BBD bucket-brigade delay (DM-2 / Deluxe
// Memory Man style). The defining BBD trait is modeled directly: the chip has
// a FIXED number of stages, so longer delay times mean a lower clock and less
// bandwidth -- the repeats get darker as the Time knob goes up (150 ms is
// bright-ish at 6 kHz, 800 ms is murky at 1.2 kHz). The loop filter is a
// steep 4th-order Butterworth lowpass (the BBD's anti-alias/reconstruction
// filters), far steeper than the digital pedal's one-pole tone, plus a
// compander-flavored tanh squash -- every pass rounds the repeats off more.
//
// Mod is the Memory Man chorus: a slow sine on the read tap (the R channel a
// quarter-cycle behind for stereo width), and it stays on during freeze so a
// frozen loop "breathes". Feedback runs to 110% for bounded self-oscillation
// (dly::softLimit). Freeze per the delay_dsp.h recipe: input muted, loop at
// exactly unity, the lossy filters and squash faded out.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "delay_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class AnalogDelay : public Effect {
public:
  AnalogDelay() : Effect("analog", "Analog") {
    time_   = addParam("time",     "Time",     "ms", 20,   800, 320);
    fb_     = addParam("feedback", "Feedback", "%",  0,    110, 45);
    mix_    = addParam("mix",      "Mix",      "%",  0,    100, 40);
    mod_    = addParam("mod",      "Mod",      "%",  0,    100, 35);
    rate_   = addParam("rate",     "Rate",     "Hz", 0.1f, 5.0f, 0.6f);
    sync_   = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_    = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 6);  // enum
    freeze_ = addParam("freeze",   "Freeze",   "",   0, 1, 0);              // toggle
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
        (int)std::ceil(sr * ((kMaxMs + kModGuardMs) / 1000.0)) + 8);
    line_.reset();
    for (int c = 0; c < 2; c++) {
      lp1_[c].reset();
      lp2_[c].reset();
      hp_[c].setCutoff(90.0, sr);
      hp_[c].reset();
      lfo_[c].setRate(rate_->get(), sr);
    }
    lfo_[1].setPhase(0.25f);  // R a quarter-cycle behind -> stereo width
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    depthS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    depthS_.snap(mod_->get() / 100.0f);
    frz_.prepare(sr);
    glide_.prepare(sr);
    glide_.snap(targetSamps());
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const float depthT = mod_->get() / 100.0f;
    const bool frozen = freeze_->get() > 0.5f;

    // The clock-rate bandwidth law, from the GLIDED time so a Time sweep
    // audibly drags the bandwidth with it: fc = 3 kHz at 300 ms, clamped to
    // 1.2..6 kHz. Two cascaded RBJ lowpasses at Butterworth Qs = 24 dB/oct.
    const float timeMs = glide_.v / (float)sr_ * 1000.0f;
    const double fc =
        std::clamp(3000.0 * 300.0 / std::max(20.0f, timeMs), 1200.0, 6000.0);
    for (int c = 0; c < 2; c++) {
      lp1_[c].setLowpass(fc, sr_, 0.541);
      lp2_[c].setLowpass(fc, sr_, 1.307);
      lfo_[c].setRate(rate_->get(), sr_);
    }
    const float depthSamps = 2.5f * 0.001f * (float)sr_;  // full-depth swing

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float depth = depthS_.next(depthT);
      const float frz = frz_.next(frozen);
      const float d = glide_.next(dTarget);
      const float loopGain = fb * (1.0f - frz) + frz;

      for (int c = 0; c < 2; c++) {
        const float in = io[c][i];
        const float wobble =
            dly::Lfo::sine(lfo_[c].next()) * depth * depthSamps;
        const float tap = line_.popSample(c, std::max(1.0f, d + wobble));

        // Write side: the BBD chain (steep LP x2, HP, squash) crossfades to
        // the raw sum as freeze engages; filters keep running to stay warm.
        const float x = in * (1.0f - frz) + tap * loopGain;
        float col = lp2_[c].process(lp1_[c].process(x));
        col = hp_[c].process(col);
        col = std::tanh(1.3f * col) * (1.0f / 1.3f);
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

  static constexpr float kMaxMs = 800.0f;
  static constexpr float kModGuardMs = 3.0f;  // 2.5 ms max swing + slack
  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  Biquad lp1_[2], lp2_[2];
  dly::HighPass1 hp_[2];
  dly::Lfo lfo_[2];
  Smoother fbS_, mixS_, depthS_;
  dly::Freeze frz_;
  dly::GlideValue glide_;
  Param *time_, *fb_, *mix_, *mod_, *rate_, *sync_, *div_, *freeze_;
};

}  // namespace fx

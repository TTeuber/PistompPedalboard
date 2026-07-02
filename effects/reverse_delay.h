// effects/reverse_delay.h -- Reverse: backwards repeats. Audio is written
// forward into a delay line while two read heads sweep AWAY from the write
// head at one sample per sample -- their delay grows at 2 samples/sample, so
// each head plays the last window of audio backwards at normal pitch. The
// heads run a half-window apart under Hann envelopes (the rv::PitchShift
// dual-tap trick, run at "ratio -1"): each envelope is exactly zero at the
// instant its head meets the write position, so the wrap point is always
// silent -- no ticks.
//
// Time (or the synced division) is the window length: how much audio gets
// flipped per swell. Feedback re-enters the write side, so every circulation
// re-reverses -- repeats alternate backwards/forwards, the classic
// regenerating reverse wash. Spread offsets the R channel's window phase so
// the two sides swell at different moments. No freeze here: the windowed
// reader has no steady circulating loop to hold at unity.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "delay_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class ReverseDelay : public Effect {
public:
  ReverseDelay() : Effect("reverse", "Reverse") {
    time_   = addParam("time",     "Time",     "ms", 100, 2000, 500);
    fb_     = addParam("feedback", "Feedback", "%",  0,   90,   30);
    mix_    = addParam("mix",      "Mix",      "%",  0,   100,  50);
    tone_   = addParam("tone",     "Tone",     "%",  0,   100,  50);
    spread_ = addParam("spread",   "Spread",   "%",  0,   100,  60);
    sync_   = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_    = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 1);  // enum
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int maxBlock) override {
    sr_ = sr;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    line_.prepare(spec);
    // The heads sweep out to twice the window length.
    maxD_ = (int)std::ceil(sr * (2.0 * kMaxMs / 1000.0)) + 8;
    line_.setMaximumDelayInSamples(maxD_);
    line_.reset();
    for (int c = 0; c < 2; c++) {
      tone1_[c].reset();
      hp_[c].setCutoff(100.0, sr);
      hp_[c].reset();
    }
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    spreadS_.prepare(sr, 100.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    spreadS_.snap(spread_->get() / 100.0f);
    glide_.prepare(sr);
    glide_.snap(targetSamps());
    phase_ = 0.0f;
  }

  void process(float* L, float* R, int n) noexcept override {
    const float nTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const float spreadT = spread_->get() / 100.0f;
    // Tone darkens the re-entering repeats, 1.2 kHz .. 12 kHz.
    const double fc = 1200.0 * std::pow(10.0, tone_->get() / 100.0);
    tone1_[0].setCutoff(std::min(fc, sr_ * 0.45), sr_);
    tone1_[1].setCutoff(std::min(fc, sr_ * 0.45), sr_);

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float N = glide_.next(nTarget);
      // R's window runs up to a half-cycle behind L's.
      const float off = 0.5f * spreadS_.next(spreadT);

      phase_ += 1.0f / std::max(1.0f, N);
      if (phase_ >= 1.0f) phase_ -= 1.0f;

      for (int c = 0; c < 2; c++) {
        const float in = io[c][i];
        float p1 = c == 0 ? phase_ : phase_ + off;
        if (p1 >= 1.0f) p1 -= 1.0f;
        float p2 = p1 + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        // Head delay grows at 2 samples/sample -> the head walks backwards
        // through the recorded audio at normal speed. The Hann gains are zero
        // exactly where a head's delay hits zero (head meets write position).
        const float d1 =
            std::clamp(2.0f * N * p1, 1.0f, (float)maxD_ - 1.0f);
        const float d2 =
            std::clamp(2.0f * N * p2, 1.0f, (float)maxD_ - 1.0f);
        // Exactly one read-pointer advance per push keeps the line stable.
        const float rev = dly::Lfo::raisedCos(p1) * line_.popSample(c, d1, false) +
                          dly::Lfo::raisedCos(p2) * line_.popSample(c, d2, true);

        // Feedback re-enters the write side, so each pass re-reverses.
        const float fbSig =
            dly::softLimit(hp_[c].process(tone1_[c].process(rev)) * fb);
        line_.pushSample(c, in + fbSig);

        io[c][i] = in * (1.0f - mix) + rev * mix;
      }
    }
  }

private:
  // Window length in samples (the synced division or the manual knob).
  float targetSamps() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get());
    return std::clamp(ms, 100.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 2000.0f;
  double sr_ = 48000.0;
  int maxD_ = 0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  OnePole tone1_[2];
  dly::HighPass1 hp_[2];
  Smoother fbS_, mixS_, spreadS_;
  dly::GlideValue glide_;
  float phase_ = 0.0f;
  Param *time_, *fb_, *mix_, *tone_, *spread_, *sync_, *div_;
};

}  // namespace fx

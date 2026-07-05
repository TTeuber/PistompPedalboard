// effects/flanger.h -- short swept comb-filter "jet plane" modulation.
//
// A modulated delay (md::ModDelay) with a resonant feedback path. The sweep is
// EXPONENTIAL: the tap runs from Manual up to Manual*(1+7*Depth) ms as
// d = manual * ratio^lfo, spending equal time per octave of comb spacing --
// a linear sweep lingers at the dull long-delay end and rushes the resonant
// short end. Feedback is BIPOLAR: negative flange is the hollow "subtractive"
// half of the classic sounds; a fixed ~7 kHz one-pole in the fed-back branch
// keeps high feedback whooshing instead of fizzing, and a soft limiter guards
// the loop (dly::softLimit's recipe). Rate can beat-sync (Sync/Div).
//
// Thru-Zero: the dry side moves onto its own delay tap at the sweep's log-
// midpoint, and the wet blend flips polarity, so once per cycle the wet sweeps
// THROUGH the dry for the tape-flange null/flip. One ~80 ms ramp drives the
// dry-tap glide and the polarity crossfade together, so the toggle is a brief
// tape-style pitch bend, never a click. (While engaged the output is time-
// shifted a few ms vs true dry, so the 10 ms bypass crossfade combs briefly --
// transient and quiet, accepted.)

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "mod_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace fx {

class Flanger : public Effect {
public:
  Flanger() : Effect("flanger", "Flanger") {
    rate_   = addParam("rate",     "Rate",      "Hz", 0.05f, 5.0f, 0.3f);
    depth_  = addParam("depth",    "Depth",     "%",  0, 100, 70);
    manual_ = addParam("manual",   "Manual",    "ms", 0.2f, 5.0f, 1.0f);
    fb_     = addParam("feedback", "Feedback",  "%",  -95, 95, 50);
    thru_   = addParam("thru",     "Thru-Zero", "",   0, 1, 0);  // toggle
    sync_   = addParam("sync",     "Sync",      "",   0, 1, 0);  // toggle
    div_    = addParam("div",      "Div",       "",   0, tempo::kDivCount - 1, 6);
    mix_    = addParam("mix",      "Mix",       "%",  0, 100, 50);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    // Longest sweep: 5 ms * ratio 8 = 40 ms; a little slack on top.
    const int maxSamps = (int)std::ceil(sr * (kMaxMs / 1000.0));
    for (int ch = 0; ch < 2; ch++) {
      wet_[ch].prepare(mono, maxSamps);
      dry_[ch].prepare(mono, maxSamps);
      damp_[ch].setCutoff(7000.0, sr);
      damp_[ch].reset();
      lfo_[ch].setPhase(ch ? 0.25f : 0.0f);  // R a quarter cycle off for width
    }
    manS_.prepare(sr, 20.0);
    manS_.snap(manual_->get() * 0.001f * (float)sr);
    lnRatioS_.prepare(sr, 20.0);
    lnRatioS_.snap(lnRatioTarget());
    fbS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
    thruS_.prepare(sr, 80.0);
    thruS_.snap(thru_->get() > 0.5f ? 1.0f : 0.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float manT = manual_->get() * 0.001f * (float)sr_;
    const float lnRatioT = lnRatioTarget();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const float thruT = thru_->get() > 0.5f ? 1.0f : 0.0f;
    const float hz =
        md::rateHz(sync_->get() > 0.5f, (int)(div_->get() + 0.5f), rate_->get());
    lfo_[0].setRate(hz, sr_);
    lfo_[1].setRate(hz, sr_);

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      // Manual/ratio/fb/mix smoothed per sample: they scale the read taps and
      // the loop gain, where block-rate steps are audible clicks.
      const float man = manS_.next(manT);
      const float lnRatio = lnRatioS_.next(lnRatioT);
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float thru = thruS_.next(thruT);
      // Dry tap: 1 sample (plain dry) gliding out to the sweep's log-midpoint
      // when Thru-Zero engages; wet polarity crossfades through zero with it.
      const float dryD = 1.0f + thru * (man * std::exp(0.5f * lnRatio) - 1.0f);
      const float wetSign = 1.0f - 2.0f * thru;

      for (int ch = 0; ch < 2; ch++) {
        const float in = io[ch][i];
        const float d = man * std::exp(lnRatio * Lfo::raisedCos(lfo_[ch].next()));
        const float w = wet_[ch].read(d);
        // Loop write: damped, soft-limited recirculation (|fb| <= 0.95).
        wet_[ch].write(softLimit(in + damp_[ch].process(w) * fb));
        dry_[ch].write(in);
        const float dd = dry_[ch].read(dryD);
        io[ch][i] = dd * (1.0f - mix) + w * wetSign * mix;
      }
    }
  }

private:
  // Sweep span: ratio 1 (no sweep) .. 8 (three octaves of comb) with Depth.
  float lnRatioTarget() const noexcept {
    return std::log(1.0f + depth_->get() / 100.0f * 7.0f);
  }
  // Unity slope below ~-6 dBFS, ceiling +8 dB -- dly::softLimit's recipe,
  // copied locally (collections don't cross-include).
  static float softLimit(float x) noexcept { return std::tanh(0.4f * x) * 2.5f; }

  static constexpr float kMaxMs = 45.0f;

  double sr_ = 48000.0;
  md::ModDelay wet_[2], dry_[2];
  OnePole damp_[2];
  Lfo lfo_[2];
  Smoother manS_, lnRatioS_, fbS_, mixS_, thruS_;
  Param *rate_, *depth_, *manual_, *fb_, *thru_, *sync_, *div_, *mix_;
};

}  // namespace fx

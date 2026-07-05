// effects/phaser.h -- swept allpass-cascade phaser (replaces the JUCE wrapper).
//
// A chain of first-order allpass stages (md::Allpass1) swept by an LFO; mixed
// against the dry signal, every stage PAIR carves one moving notch. Stages
// picks 4/6/8 (Phase 90 grit up to lush rack sweep) -- all eight stages always
// run and the wet tap crossfades between the stage-4/6/8 outputs, so switching
// is click-free with no reset. Centre replaces the old fixed 600 Hz: the sweep
// breathes +/-1.5 octaves around it (scaled by Depth) EXPONENTIALLY, which
// spends equal time per octave -- the musical sweep a linear LFO can't make.
// Feedback returns the wet tap into stage 1 (<= 0.9; an allpass chain has unit
// magnitude, so it can peak but never run away). Rate can beat-sync (Sync/Div,
// one sweep per division), same recipe as Tremolo/Delay.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "mod_dsp.h"

#include <cmath>

namespace fx {

class Phaser : public Effect {
public:
  Phaser() : Effect("phaser", "Phaser") {
    rate_   = addParam("rate",     "Rate",     "Hz", 0.05f, 8.0f, 0.5f);
    depth_  = addParam("depth",    "Depth",    "%",  0, 100, 50);
    fb_     = addParam("feedback", "Feedback", "%",  0, 100, 30);
    centre_ = addParam("centre",   "Centre",   "Hz", 100, 2000, 600);
    stages_ = addParam("stages",   "Stages",   "",   0, 2, 0);  // enum: 4/6/8
    sync_   = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_    = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 6);
    mix_    = addParam("mix",      "Mix",      "%",  0, 100, 50);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    for (int ch = 0; ch < 2; ch++) {
      for (int s = 0; s < kStages; s++) ap_[ch][s].reset();
      fbState_[ch] = 0.0f;
      lfo_[ch].setPhase(ch ? 0.25f : 0.0f);  // R a quarter cycle off for width
    }
    cLogS_.prepare(sr, 30.0);
    cLogS_.snap(std::log2(centre_->get()));
    depthOctS_.prepare(sr, 20.0);
    depthOctS_.snap(depth_->get() / 100.0f * kOctaves);
    fbS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f * 0.9f);
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
    const int tap = (int)(stages_->get() + 0.5f);
    for (int t = 0; t < kTaps; t++) {
      tapS_[t].prepare(sr, 30.0);
      tapS_[t].snap(t == tap ? 1.0f : 0.0f);
    }
  }

  void process(float* L, float* R, int n) noexcept override {
    const float cLogT = std::log2(centre_->get());
    const float depthOctT = depth_->get() / 100.0f * kOctaves;
    const float fbT = fb_->get() / 100.0f * 0.9f;  // keep resonant, not runaway
    const float mixT = mix_->get() / 100.0f;
    const int tap = (int)(stages_->get() + 0.5f);
    const float hz =
        md::rateHz(sync_->get() > 0.5f, (int)(div_->get() + 0.5f), rate_->get());
    lfo_[0].setRate(hz, sr_);
    lfo_[1].setRate(hz, sr_);

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      // Centre/depth smoothed per sample in the log domain (a block-rate knob
      // step would jump every stage's coefficient at once); fb/mix/tap gains
      // are plain wet-path gains.
      const float cLog = cLogS_.next(cLogT);
      const float depthOct = depthOctS_.next(depthOctT);
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      float g[kTaps];
      for (int t = 0; t < kTaps; t++) g[t] = tapS_[t].next(t == tap ? 1.0f : 0.0f);

      for (int ch = 0; ch < 2; ch++) {
        const float bip = Lfo::sine(lfo_[ch].next());
        // One coefficient per channel per sample, shared by all its stages.
        const float a =
            md::Allpass1::coef(std::exp2(cLog + depthOct * bip), sr_);
        const float in = io[ch][i];
        float x = in + fbState_[ch] * fb;
        float taps[kTaps];
        for (int s = 0; s < kStages; s++) {
          ap_[ch][s].setCoef(a);
          x = ap_[ch][s].process(x);
          if (s == 3) taps[0] = x;
          else if (s == 5) taps[1] = x;
        }
        taps[2] = x;
        const float wet = taps[0] * g[0] + taps[1] * g[1] + taps[2] * g[2];
        fbState_[ch] = wet;
        io[ch][i] = in * (1.0f - mix) + wet * mix;
      }
    }
  }

private:
  static constexpr int kStages = 8;
  static constexpr int kTaps = 3;         // wet tap after stage 4 / 6 / 8
  static constexpr float kOctaves = 1.5f; // sweep swing at full Depth

  double sr_ = 48000.0;
  md::Allpass1 ap_[2][kStages];
  Lfo lfo_[2];
  float fbState_[2] = {0, 0};
  Smoother cLogS_, depthOctS_, fbS_, mixS_, tapS_[kTaps];
  Param *rate_, *depth_, *fb_, *centre_, *stages_, *sync_, *div_, *mix_;
};

}  // namespace fx

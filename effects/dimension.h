// effects/dimension.h -- Dimension-style stereo widener (Roland SDD-320).
//
// Two delay lines modulated in ANTIPHASE, each side's wet cross-mixed with a
// negative helping of the other's: the comb peaks of one channel land on the
// other's notches, which the ear reads as width, not warble. Depths are tiny
// (well under a ms) and the wet is BBD-dark, so it disappears into the dry --
// the always-on studio bloom, deliberately subtler than the Chorus pedal.
//
// Faithful to the original's four-buttons-and-nothing-else panel: Mode picks a
// preset rate/depth pair (1 subtle .. 4 wide), Mix trims the effect in. Mode
// changes glide through smoothers, so stepping 1..4 slews instead of clicking.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "mod_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace fx {

class Dimension : public Effect {
public:
  Dimension() : Effect("dimension", "Dimension") {
    dmode_ = addParam("dmode", "Mode", "",  0, 3,   1);  // enum: 1/2/3/4
    mix_   = addParam("mix",   "Mix",  "%", 0, 100, 100);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    const int maxSamps =
        (int)std::ceil(sr * ((kBaseMs + kDepthMs[3] + 0.5) / 1000.0));
    for (int ch = 0; ch < 2; ch++) {
      line_[ch].prepare(mono, maxSamps);
      dark_[ch].setCutoff(9000.0, sr);
      dark_[ch].reset();
    }
    baseSamps_ = (float)(kBaseMs * 0.001 * sr);
    lfo_.setPhase(0.0f);
    const int m = (int)(dmode_->get() + 0.5f);
    depthS_.prepare(sr, 100.0);  // slow: button presses slew, never click
    depthS_.snap((float)(kDepthMs[m] * 0.001 * sr));
    mS_.prepare(sr, 20.0);
    mS_.snap(mix_->get() / 100.0f * kTrim);
  }

  void process(float* L, float* R, int n) noexcept override {
    const int m = (int)(dmode_->get() + 0.5f);
    const float depthT = (float)(kDepthMs[m] * 0.001 * sr_);
    const float mT = mix_->get() / 100.0f * kTrim;
    lfo_.setRate(kRate[m], sr_);  // phase-continuous; rate steps are inaudible

    for (int i = 0; i < n; i++) {
      const float depth = depthS_.next(depthT);
      const float mm = mS_.next(mT);
      // One LFO, two antiphase taps: L stretches while R shrinks.
      const float s = Lfo::sine(lfo_.next());
      const float inL = L[i], inR = R[i];
      line_[0].write(inL);
      line_[1].write(inR);
      const float wetL = dark_[0].process(line_[0].read(baseSamps_ + depth * s));
      const float wetR = dark_[1].process(line_[1].read(baseSamps_ - depth * s));
      // The widener core: each side gets its wet minus a helping of the other.
      L[i] = inL + mm * (wetL - kCross * wetR);
      R[i] = inR + mm * (wetR - kCross * wetL);
    }
  }

private:
  static constexpr double kBaseMs = 4.0;
  // Preset rate (Hz) / depth (ms) pairs for Modes 1..4.
  static constexpr float kRate[4] = {0.22f, 0.32f, 0.45f, 0.65f};
  static constexpr double kDepthMs[4] = {0.35, 0.55, 0.80, 1.20};
  static constexpr float kCross = 0.7f;
  static constexpr float kTrim = 0.7f;  // engaged sits at ~unity loudness

  double sr_ = 48000.0;
  md::ModDelay line_[2];
  OnePole dark_[2];
  Lfo lfo_;
  float baseSamps_ = 0.0f;
  Smoother depthS_, mS_;
  Param *dmode_, *mix_;
};

}  // namespace fx

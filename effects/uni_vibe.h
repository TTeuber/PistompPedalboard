// effects/uni_vibe.h -- Uni-Vibe: staggered-allpass photocell modulation.
//
// Four md::Allpass1 stages per side whose corner frequencies are STAGGERED
// (~110/280/700/1800 Hz, the mismatched-capacitor signature) and swept
// together: unlike a phaser's identical stages, the notches never track
// harmonically, which is why a vibe reads as watery chorus-with-throb rather
// than a sweep. The LFO is the lamp/LDR bulb: a skewed raised-cosine that
// blooms fast and decays slow (md::skewedCos), shared by both channels so the
// pulse reads as one throb (and costs half the trig). Voice picks the two
// classic outputs -- Chorus (50/50 dry+wet, the phasey shimmer) or Vibrato
// (wet only, pure pitch-bend illusion) -- through smoothed gains, click-free.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "mod_dsp.h"

#include <cmath>

namespace fx {

class UniVibe : public Effect {
public:
  UniVibe() : Effect("vibe", "Uni-Vibe") {
    rate_  = addParam("rate",  "Rate",  "Hz", 0.1f, 8.0f, 0.9f);
    depth_ = addParam("depth", "Depth", "%",  0, 100, 60);
    voice_ = addParam("voice", "Voice", "",   0, 1,   0);  // enum: Chorus/Vibrato
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    for (int ch = 0; ch < 2; ch++)
      for (int s = 0; s < kStages; s++) ap_[ch][s].reset();
    lfo_.setPhase(0.0f);
    depthOctS_.prepare(sr, 20.0);
    depthOctS_.snap(depth_->get() / 100.0f * kOctaves);
    const bool vib = voice_->get() > 0.5f;
    dryGS_.prepare(sr, 20.0);
    dryGS_.snap(vib ? 0.0f : 0.5f);
    wetGS_.prepare(sr, 20.0);
    wetGS_.snap(vib ? 1.0f : 0.5f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float depthOctT = depth_->get() / 100.0f * kOctaves;
    const bool vib = voice_->get() > 0.5f;
    const float dryT = vib ? 0.0f : 0.5f;
    const float wetT = vib ? 1.0f : 0.5f;
    lfo_.setRate(rate_->get(), sr_);

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      // The bulb: fast bloom, slow tail, remapped to +/-1.
      const float bip = 2.0f * md::skewedCos(lfo_.next(), 1.5f) - 1.0f;
      const float depthOct = depthOctS_.next(depthOctT);
      const float dryG = dryGS_.next(dryT);
      const float wetG = wetGS_.next(wetT);
      // Four staggered corners swept together; coefs shared by both channels.
      float a[kStages];
      for (int s = 0; s < kStages; s++)
        a[s] = md::Allpass1::coef(kFc[s] * std::exp2(depthOct * bip), sr_);

      for (int ch = 0; ch < 2; ch++) {
        float x = io[ch][i];
        const float in = x;
        for (int s = 0; s < kStages; s++) {
          ap_[ch][s].setCoef(a[s]);
          x = ap_[ch][s].process(x);
        }
        io[ch][i] = in * dryG + x * wetG;
      }
    }
  }

private:
  static constexpr int kStages = 4;
  // Staggered stage corners -- the mismatched caps of the original.
  static constexpr float kFc[kStages] = {110.0f, 280.0f, 700.0f, 1800.0f};
  static constexpr float kOctaves = 1.2f;  // sweep swing at full Depth

  double sr_ = 48000.0;
  md::Allpass1 ap_[2][kStages];
  Lfo lfo_;
  Smoother depthOctS_, dryGS_, wetGS_;
  Param *rate_, *depth_, *voice_;
};

}  // namespace fx

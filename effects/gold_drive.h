// effects/gold_drive.h -- Klon-family "transparent" overdrive.
//
// The gold-box circuit everyone chases is really TWO pedals sharing a knob: a
// clean boost path and a hard-clipped dirt path, mixed by the Gain control.
// At low gain it's almost all clean boost -- which is why it earned the
// "transparent, always-on" reputation -- and the dirt fades in on top as you
// turn up. We map one smoothed d = Drive/100 to both arms: dirt mix m = d^2
// (quadratic, so the bottom third of the knob stays a boost) and dirt gain
// g = 1 + 60 d^2 (up to ~+36 dB into +-0.9 hard clip). The dirt path is
// highpassed at 350 Hz -- looser than a TS's 720, keeping more body, which is
// the other half of "transparent". Tone is a treble shelf (+-8 dB @ 1.8 kHz),
// flat at noon.
//
// 2x oversampled (the dirt path hard-clips, but at moderate gain); DC blocker
// as insurance. The dirt arm carries its own makeup ~ g^-0.5 so the blend of
// the two arms stays balanced across the sweep.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "drive_dsp.h"

#include <cmath>

namespace fx {

class GoldDrive : public Effect {
public:
  GoldDrive() : Effect("golddrive", "Gold Drive") {
    drive_ = addParam("drive", "Drive", "%", 0, 100, 30);
    tone_  = addParam("tone",  "Tone",  "%", 0, 100, 50);
    level_ = addParam("level", "Level", "%", 0, 100, 60);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr, 2);
    osR_.prepare(sr, 2);
    // Inside the shaper functor => cutoff lives at the oversampled rate.
    hpL_.setCutoff(350.0, sr * 2.0);
    hpR_.setCutoff(350.0, sr * 2.0);
    hpL_.reset();
    hpR_.reset();
    shelfL_.reset();
    shelfR_.reset();
    dcL_.prepare(sr);
    dcR_.prepare(sr);
    driveS_.prepare(sr, 20.0);
    makeupS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    driveS_.snap(drive_->get() / 100.0f);
    makeupS_.snap(dirtMakeup(drive_->get() / 100.0f));
    levelS_.snap(level_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dT = drive_->get() / 100.0f;
    const float mkT = dirtMakeup(dT);
    const float levelT = level_->get() / 100.0f;
    // Tone: treble shelf, cut..boost +-8 dB, flat at 50.
    const double shelfDb = (tone_->get() - 50.0) / 50.0 * 8.0;
    shelfL_.setHighShelf(1800.0, sr_, shelfDb);
    shelfR_.setHighShelf(1800.0, sr_, shelfDb);

    for (int i = 0; i < n; i++) {
      const float d = driveS_.next(dT);
      const float mk = makeupS_.next(mkT);
      const float lvl = levelS_.next(levelT);
      const float m = d * d;            // dirt mix: quadratic fade-in
      const float g = 1.0f + 60.0f * m; // dirt gain, up to ~+36 dB
      const float clean = (1.0f + 2.0f * d) * (1.0f - m);

      float yl = osL_.process(L[i], [this, clean, m, g, mk](float u) noexcept {
        return clean * u +
               m * mk * drv::hardClip(g * hpL_.process(u), 0.9f, -0.9f);
      });
      float yr = osR_.process(R[i], [this, clean, m, g, mk](float u) noexcept {
        return clean * u +
               m * mk * drv::hardClip(g * hpR_.process(u), 0.9f, -0.9f);
      });
      L[i] = shelfL_.process(dcL_.process(yl)) * lvl;
      R[i] = shelfR_.process(dcR_.process(yr)) * lvl;
    }
  }

private:
  // Makeup for the dirt arm only (the clean arm is already level-honest).
  static float dirtMakeup(float d) noexcept {
    const float g = 1.0f + 60.0f * d * d;
    return 3.0f * std::pow(g, -0.50f);
  }

  double sr_ = 48000.0;
  drv::OversamplerIIR osL_, osR_;
  drv::HighPass1 hpL_, hpR_;   // dirt-path HP (looser than TS), at OS rate
  Biquad shelfL_, shelfR_;     // treble shelf tone
  drv::DcBlock dcL_, dcR_;
  Smoother driveS_, makeupS_, levelS_;
  Param *drive_, *tone_, *level_;
};

}  // namespace fx

// effects/overdrive.h -- Tube Screamer-family overdrive.
//
// The TS trick is the op-amp clipper with diodes in the FEEDBACK path: the
// clean signal passes at unity and the clipped, gain-boosted band rides on top
// of it -- shape(u) = u + tanh(g * hp(u)). The highpass at ~720 Hz *into* the
// clipper is what makes the voice: lows stay tight (they never hit the diodes)
// and the drive lands on the mids -- the famous mid-hump. A fixed 6 kHz
// lowpass after the shaper stands in for the op-amp's closing bandwidth, then
// the Tone knob sweeps a one-pole rolloff. Blend mixes the untouched input
// back in (a parallel clean loop -- at 100 you get the whole pedal).
//
// The whole nonlinear path runs 2x oversampled (drive_dsp.h); a DC blocker
// follows as cheap insurance. Output makeup ~ g^-0.38 keeps perceived level
// roughly flat across the Drive sweep.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "drive_dsp.h"

#include <cmath>

namespace fx {

class Overdrive : public Effect {
public:
  Overdrive() : Effect("overdrive", "Overdrive") {
    drive_ = addParam("drive", "Drive", "%", 0, 100, 45);
    tone_  = addParam("tone",  "Tone",  "%", 0, 100, 55);
    blend_ = addParam("blend", "Blend", "%", 0, 100, 100);
    level_ = addParam("level", "Level", "%", 0, 100, 70);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr, 2);
    osR_.prepare(sr, 2);
    // Inside the shaper functor => cutoffs live at the oversampled rate.
    hpL_.setCutoff(720.0, sr * 2.0);
    hpR_.setCutoff(720.0, sr * 2.0);
    hpL_.reset();
    hpR_.reset();
    lp6kL_.setCutoff(6000.0, sr);
    lp6kR_.setCutoff(6000.0, sr);
    lp6kL_.reset();
    lp6kR_.reset();
    toneL_.reset();
    toneR_.reset();
    dcL_.prepare(sr);
    dcR_.prepare(sr);
    gainS_.prepare(sr, 20.0);
    blendS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    makeupS_.prepare(sr, 20.0);
    gainS_.snap(driveGain());
    blendS_.snap(blend_->get() / 100.0f);
    levelS_.snap(level_->get() / 100.0f);
    makeupS_.snap(makeup(driveGain()));
  }

  void process(float* L, float* R, int n) noexcept override {
    const float gT = driveGain();
    const float mkT = makeup(gT);
    const float blendT = blend_->get() / 100.0f;
    const float levelT = level_->get() / 100.0f;
    // Tone: log sweep 800 Hz (dark) .. 8 kHz (open).
    const double fc = 800.0 * std::pow(10.0, tone_->get() / 100.0);
    toneL_.setCutoff(fc, sr_);
    toneR_.setCutoff(fc, sr_);

    for (int i = 0; i < n; i++) {
      const float g = gainS_.next(gT);
      const float b = blendS_.next(blendT);
      const float lvl = levelS_.next(levelT);
      const float mk = makeupS_.next(mkT);
      const float dryL = L[i], dryR = R[i];

      float yl = osL_.process(dryL, [this, g](float u) noexcept {
        return u + std::tanh(g * hpL_.process(u));
      });
      float yr = osR_.process(dryR, [this, g](float u) noexcept {
        return u + std::tanh(g * hpR_.process(u));
      });
      yl = dcL_.process(toneL_.process(lp6kL_.process(yl))) * mk;
      yr = dcR_.process(toneR_.process(lp6kR_.process(yr))) * mk;
      L[i] = ((1.0f - b) * dryL + b * yl) * lvl;
      R[i] = ((1.0f - b) * dryR + b * yr) * lvl;
    }
  }

private:
  // Drive -> clipper gain, +6..+40 dB.
  float driveGain() const noexcept {
    return std::pow(10.0f, (6.0f + 34.0f * drive_->get() / 100.0f) / 20.0f);
  }
  // Loudness compensation: measured raw growth is ~13 dB across the 34 dB
  // Drive range (tanh compresses the rest), so g^-0.38 flattens it. Tuned
  // with the render sweep (pedalboard_render --drive overdrive --sweep) to
  // ~2.3 dB total spread.
  static float makeup(float g) noexcept {
    return 2.45f * std::pow(g, -0.38f);
  }

  double sr_ = 48000.0;
  drv::OversamplerIIR osL_, osR_;
  drv::HighPass1 hpL_, hpR_;   // pre-clipper HP (mid-hump), at OS rate
  OnePole lp6kL_, lp6kR_;      // fixed op-amp bandwidth rolloff
  OnePole toneL_, toneR_;      // Tone knob
  drv::DcBlock dcL_, dcR_;
  Smoother gainS_, blendS_, levelS_, makeupS_;
  Param *drive_, *tone_, *blend_, *level_;
};

}  // namespace fx

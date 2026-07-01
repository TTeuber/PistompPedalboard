// effects/distortion.h -- RAT-family hard-clip distortion.
//
// The RAT recipe: a huge op-amp gain stage (here +12..+60 dB) slamming into
// hard clipping diodes, with a highpass into the gain so the low end stays
// tight ("chug" instead of mud), then the signature "Filter" control -- a
// plain lowpass you CLOSE for darker, not a tone stack. The clip is slightly
// asymmetric (+1.0 / -0.85 rails) for even-harmonic grit; that rectification
// puts real DC on the output, so a DC blocker sits right after the shaper --
// this replaces the old placeholder whose tanh(v + 0.15v^2) shipped its DC
// straight down the chain.
//
// The clipper runs 4x oversampled (drive_dsp.h): at max gain the output is
// near-square and the harmonics run high enough that 2x isn't enough. Output
// makeup ~ g^-0.15 keeps the Drive sweep at roughly constant loudness.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "drive_dsp.h"

#include <cmath>

namespace fx {

class Distortion : public Effect {
public:
  Distortion() : Effect("distortion", "Distortion") {
    drive_ = addParam("drive", "Drive",  "%", 0, 100, 50);
    tone_  = addParam("tone",  "Filter", "%", 0, 100, 50);
    level_ = addParam("level", "Level",  "%", 0, 100, 65);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr, 4);
    osR_.prepare(sr, 4);
    // Inside the shaper functor => cutoff lives at the oversampled rate.
    hpL_.setCutoff(100.0, sr * 4.0);
    hpR_.setCutoff(100.0, sr * 4.0);
    hpL_.reset();
    hpR_.reset();
    toneL_.reset();
    toneR_.reset();
    dcL_.prepare(sr);
    dcR_.prepare(sr);
    gainS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    gainS_.snap(driveGain());
    levelS_.snap(levelTarget(driveGain()));
  }

  void process(float* L, float* R, int n) noexcept override {
    const float gT = driveGain();
    const float levelT = levelTarget(gT);
    // Filter: log sweep 500 Hz (closed/dark) .. 12 kHz (open).
    const double fc = 500.0 * std::pow(24.0, tone_->get() / 100.0);
    toneL_.setCutoff(fc, sr_);
    toneR_.setCutoff(fc, sr_);

    for (int i = 0; i < n; i++) {
      const float g = gainS_.next(gT);
      const float lvl = levelS_.next(levelT);

      float yl = osL_.process(L[i], [this, g](float u) noexcept {
        return drv::hardClip(g * hpL_.process(u), 1.0f, -0.85f);
      });
      float yr = osR_.process(R[i], [this, g](float u) noexcept {
        return drv::hardClip(g * hpR_.process(u), 1.0f, -0.85f);
      });
      L[i] = toneL_.process(dcL_.process(yl)) * lvl;
      R[i] = toneR_.process(dcR_.process(yr)) * lvl;
    }
  }

private:
  // Drive -> clipper gain, +12..+60 dB.
  float driveGain() const noexcept {
    return std::pow(10.0f, (12.0f + 48.0f * drive_->get() / 100.0f) / 20.0f);
  }
  // Level knob with loudness compensation folded in. Above ~25% drive the
  // clip is pinned to the rails and raw RMS barely grows, so alpha is small
  // (0.15, measured with the render sweep) -- most of the "compensation" is
  // just the constant.
  float levelTarget(float g) const noexcept {
    return level_->get() / 100.0f * 0.85f * std::pow(g, -0.15f);
  }

  double sr_ = 48000.0;
  drv::OversamplerIIR osL_, osR_;
  drv::HighPass1 hpL_, hpR_;   // pre-gain tightness HP, at OS rate
  OnePole toneL_, toneR_;      // the "Filter" knob
  drv::DcBlock dcL_, dcR_;     // mandatory: the clip rails are asymmetric
  Smoother gainS_, levelS_;
  Param *drive_, *tone_, *level_;
};

}  // namespace fx

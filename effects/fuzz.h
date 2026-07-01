// effects/fuzz.h -- Fuzz Face-family germanium fuzz.
//
// What separates a fuzz from a loud overdrive is BIAS MOVEMENT: in the real
// two-transistor circuit, playing hard shoves the second transistor's
// operating point toward a rail, so the waveform clips asymmetrically,
// sputters, and gates off as the note decays -- and rolling the guitar volume
// down moves the bias back and the same pedal goes nearly clean. We model
// that with an envelope follower (3 ms attack / 120 ms release) on the input
// driving a bias offset into a tanh clipper: shape(u) = biasClip(g*u, offset)
// where offset grows with both Drive and how hard you're playing. The Bias
// knob scales the effect -- low = smooth and symmetric, high = strangled,
// gated sputter.
//
// No input highpass on purpose (a fuzz eats the full-range guitar signal --
// that interaction is the point). The moving bias generates real DC, so the
// DC blocker after the shaper is mandatory. 4x oversampled: near-square
// waves at high drive. Makeup ~ g^-0.40 flattens the Drive sweep.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "drive_dsp.h"

#include <algorithm>
#include <cmath>

namespace fx {

class Fuzz : public Effect {
public:
  Fuzz() : Effect("fuzz", "Fuzz") {
    drive_ = addParam("drive", "Drive", "%", 0, 100, 60);
    bias_  = addParam("bias",  "Bias",  "%", 0, 100, 55);
    tone_  = addParam("tone",  "Tone",  "%", 0, 100, 70);
    level_ = addParam("level", "Level", "%", 0, 100, 60);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr, 4);
    osR_.prepare(sr, 4);
    env_.prepare(sr, 3.0, 120.0);
    toneL_.reset();
    toneR_.reset();
    dcL_.prepare(sr);
    dcR_.prepare(sr);
    gainS_.prepare(sr, 20.0);
    biasS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    gainS_.snap(driveGain());
    biasS_.snap(biasAmount());
    levelS_.snap(levelTarget(driveGain()));
  }

  void process(float* L, float* R, int n) noexcept override {
    const float gT = driveGain();
    const float biasT = biasAmount();
    const float levelT = levelTarget(gT);
    // Tone: log sweep 1 kHz (woolly) .. 10 kHz (raw).
    const double fc = 1000.0 * std::pow(10.0, tone_->get() / 100.0);
    toneL_.setCutoff(fc, sr_);
    toneR_.setCutoff(fc, sr_);

    for (int i = 0; i < n; i++) {
      const float g = gainS_.next(gT);
      const float B = biasS_.next(biasT);
      const float lvl = levelS_.next(levelT);

      // One shared bias for both channels (the input is a mono guitar fanned
      // to stereo; splitting the envelope would decorrelate the sputter).
      const float e = env_.process(std::max(std::fabs(L[i]), std::fabs(R[i])));
      const float offset =
          std::clamp(B * (0.15f + 2.0f * g * e), 0.0f, 2.5f);

      float yl = osL_.process(L[i], [g, offset](float u) noexcept {
        return drv::biasClip(g * u, offset);
      });
      float yr = osR_.process(R[i], [g, offset](float u) noexcept {
        return drv::biasClip(g * u, offset);
      });
      L[i] = toneL_.process(dcL_.process(yl)) * lvl;
      R[i] = toneR_.process(dcR_.process(yr)) * lvl;
    }
  }

private:
  // Drive -> clipper gain, +10..+40 dB.
  float driveGain() const noexcept {
    return std::pow(10.0f, (10.0f + 30.0f * drive_->get() / 100.0f) / 20.0f);
  }
  // Bias knob -> bias-shift scale, 0.1 (barely moves) .. 1.0 (full sputter).
  float biasAmount() const noexcept {
    return 0.1f + 0.9f * bias_->get() / 100.0f;
  }
  // Level knob with loudness compensation folded in.
  float levelTarget(float g) const noexcept {
    return level_->get() / 100.0f * 3.2f * std::pow(g, -0.40f);
  }

  double sr_ = 48000.0;
  drv::OversamplerIIR osL_, osR_;
  drv::EnvFollower env_;       // input envelope -> bias shift (base rate)
  OnePole toneL_, toneR_;
  drv::DcBlock dcL_, dcR_;     // mandatory: the moving bias generates DC
  Smoother gainS_, biasS_, levelS_;
  Param *drive_, *bias_, *tone_, *level_;
};

}  // namespace fx

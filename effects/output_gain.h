// effects/output_gain.h -- output level trim at the very end of the chain.
//
// The "Output Level" control on the device's Output page, sitting after the amp
// and EQ. A plain stereo gain in dB to set the final level into the interface --
// the mirror of input_gain.h at the other end. 0 dB is unity (true bypass). The
// linear gain is one-pole smoothed per sample (~20 ms) so a swept knob glides
// instead of stepping per block -- a per-block jump in a flat multiplier is a
// waveform discontinuity, i.e. an audible click. This is the chain's last effect;
// the global Master fader still scales everything after it.

#pragma once

#include "../effect.h"

#include <algorithm>
#include <cmath>

namespace fx {

class OutputGain : public Effect {
public:
  OutputGain() : Effect("output", "Output") {
    gain_ = addParam("gain", "Gain", "dB", -24, 24, 0);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    cur_ = std::pow(10.0f, gain_->get() / 20.0f);  // seed at target -> no startup ramp
  }

  void process(float* L, float* R, int n) noexcept override {
    const float target = std::pow(10.0f, gain_->get() / 20.0f);
    const float a = std::exp(-1.0f / (float(sr_) * 0.001f * 20.0f));  // ~20 ms
    for (int i = 0; i < n; i++) {
      cur_ = a * cur_ + (1.0f - a) * target;
      L[i] *= cur_;
      R[i] *= cur_;
    }
  }

private:
  Param* gain_;
  double sr_ = 48000.0;
  float cur_ = 1.0f;  // smoothed linear gain, carried across blocks
};

}  // namespace fx

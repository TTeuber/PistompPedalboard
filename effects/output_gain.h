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
#include "../dsp_util.h"

#include <cmath>

namespace fx {

class OutputGain : public Effect {
public:
  OutputGain() : Effect("output", "Output") {
    gain_ = addParam("gain", "Gain", "dB", -24, 24, 0);
  }

  void prepare(double sr, int) override {
    gainS_.prepare(sr, 20.0);
    gainS_.snap(std::pow(10.0f, gain_->get() / 20.0f));  // seed at target -> no startup ramp
  }

  void process(float* L, float* R, int n) noexcept override {
    const float target = std::pow(10.0f, gain_->get() / 20.0f);
    for (int i = 0; i < n; i++) {
      const float g = gainS_.next(target);
      L[i] *= g;
      R[i] *= g;
    }
  }

private:
  Param* gain_;
  Smoother gainS_;  // smoothed linear gain, carried across blocks
};

}  // namespace fx

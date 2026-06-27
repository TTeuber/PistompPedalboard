// effects/output_gain.h -- output level trim at the very end of the chain.
//
// The "Output Level" control on the device's Output page, sitting after the amp
// and EQ. A plain stereo gain in dB to set the final level into the interface --
// the mirror of input_gain.h at the other end. 0 dB is unity (true bypass); no
// smoothing needed (a set-and-forget trim, not a swept knob). This is the chain's
// last effect; the global Master fader still scales everything after it.

#pragma once

#include "../effect.h"

#include <cmath>

namespace fx {

class OutputGain : public Effect {
public:
  OutputGain() : Effect("output", "Output") {
    gain_ = addParam("gain", "Gain", "dB", -24, 24, 0);
  }

  void prepare(double, int) override {}

  void process(float* L, float* R, int n) noexcept override {
    const float g = std::pow(10.0f, gain_->get() / 20.0f);
    for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
  }

private:
  Param* gain_;
};

}  // namespace fx

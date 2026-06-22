// effects/input_gain.h -- input level trim at the very front of the chain.
//
// The "Input Level" control on the device's Input page. A plain stereo gain in dB
// applied before the gate/comp see the signal, so you can match a hot or weak
// pickup into the rest of the chain. 0 dB is unity (true bypass); kept dead simple
// (no smoothing needed -- it's a set-and-forget trim, not a swept knob).

#pragma once

#include "../effect.h"

#include <cmath>

namespace fx {

class InputGain : public Effect {
public:
  InputGain() : Effect("input", "Input Level") {
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

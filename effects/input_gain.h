// effects/input_gain.h -- input level trim at the very front of the chain.
//
// The "Input Level" control on the device's Input page. A plain stereo gain in dB
// applied before the gate/comp see the signal, so you can match a hot or weak
// pickup into the rest of the chain. 0 dB is unity (true bypass). The linear gain
// is one-pole smoothed per sample (~20 ms) so a swept knob glides instead of
// stepping per block (a per-block jump in a flat multiplier clicks); mirror of
// output_gain.h at the other end of the chain.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <cmath>

namespace fx {

class InputGain : public Effect {
public:
  InputGain() : Effect("input", "Input") {
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

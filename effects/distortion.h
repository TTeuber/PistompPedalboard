// effects/distortion.h -- harder, slightly asymmetric clip (split from Drive).
//
// More aggressive than the Overdrive: a tanh with a squared term added for
// even-harmonic grit and asymmetry, the tighter high-gain rhythm/lead voice.
// Shared circuit lives in drive_base.h.

#pragma once

#include "drive_base.h"

#include <cmath>

namespace fx {

class Distortion : public DriveBase<Distortion> {
public:
  Distortion() : DriveBase("distortion", "Distortion", 55) {}

  static float shape(float v) noexcept { return std::tanh(v + 0.15f * v * v); }
};

}  // namespace fx

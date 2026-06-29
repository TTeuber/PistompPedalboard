// effects/overdrive.h -- smooth soft-clip drive (split out of the old Drive pedal).
//
// The gentlest of the three gain pedals: a plain tanh soft clip, the classic
// "amp pushed a little harder" voice worship rhythm parts live in. See
// drive_base.h for the shared circuit; this only supplies the nonlinearity.

#pragma once

#include "drive_base.h"

#include <cmath>

namespace fx {

class Overdrive : public DriveBase<Overdrive> {
public:
  Overdrive() : DriveBase("overdrive", "Overdrive", 40) {}

  static float shape(float v) noexcept { return std::tanh(v); }
};

}  // namespace fx

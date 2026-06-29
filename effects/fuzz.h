// effects/fuzz.h -- aggressive near-square clip (split from Drive).
//
// The wildest of the three: a hard-driven tanh that squares off into a
// gated-sounding fuzz. Shared circuit lives in drive_base.h.

#pragma once

#include "drive_base.h"

#include <cmath>

namespace fx {

class Fuzz : public DriveBase<Fuzz> {
public:
  Fuzz() : DriveBase("fuzz", "Fuzz", 70) {}

  static float shape(float v) noexcept { return std::tanh(3.0f * v); }
};

}  // namespace fx

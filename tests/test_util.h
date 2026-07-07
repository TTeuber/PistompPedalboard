// test_util.h -- shared helpers for the pedalboard test suite.

#pragma once

#include "effect.h"
#include "fx_factory.h"
#include "fx_registry.h"

#include <cmath>
#include <cstdint>

namespace testutil {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Deterministic noise (no rand(): runs must be reproducible).
struct Lcg {
  uint32_t s;
  explicit Lcg(uint32_t seed) : s(seed) {}
  float next() {  // uniform in [-1, 1)
    s = s * 1664525u + 1013904223u;
    return (float)(int32_t)s * (1.0f / 2147483648.0f);
  }
};

// Test signal: 220 Hz sine + broadband noise, distinct per channel, peak ~0.8.
struct SignalGen {
  Lcg lcgL{0x12345677u}, lcgR{0x89abcdefu};
  double phase = 0.0;
  void fill(float* L, float* R, int n) {
    constexpr double kTwoPi = 6.283185307179586;
    for (int i = 0; i < n; i++) {
      const float s = 0.5f * (float)std::sin(phase);
      phase += kTwoPi * 220.0 / kRate;
      L[i] = s + 0.3f * lcgL.next();
      R[i] = s + 0.3f * lcgR.next();
    }
  }
};

// Index of the first NaN/inf/out-of-bounds sample, or -1 if the block is clean.
inline int firstBad(const float* x, int n, float limit) {
  for (int i = 0; i < n; i++)
    if (!std::isfinite(x[i]) || std::fabs(x[i]) > limit) return i;
  return -1;
}

inline float peakAbs(const float* x, int n) {
  float m = 0.0f;
  for (int i = 0; i < n; i++) m = std::max(m, std::fabs(x[i]));
  return m;
}

// One registry shared by tests that only mint instances (the per-kind id
// counters create() bumps don't matter to them). Tests about ids build their
// own factory instead.
inline FxFactory& registry() {
  static FxFactory f;
  static const bool once = (registerAllFx(f), true);
  (void)once;
  return f;
}

}  // namespace testutil

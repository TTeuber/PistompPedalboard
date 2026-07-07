// test_dsp_invariants.cpp -- offline DSP sanity sweeps over every FX kind.
//
// Everything here is table-driven off the shared registry (fx_registry.cpp),
// so a newly registered effect gets the full battery automatically:
//   * no-NaN/no-inf/no-blowup with each param at min/mid/max,
//   * silence in -> silence out (non-tails effects, default settings),
//   * bypass is a null-op (Effect::run with enabled=false),
//   * delay/reverb tails decay to silence at default settings.
//
// Thresholds are deliberately generous: these are invariant tests ("did the
// filter blow up / denormal / NaN"), not golden-audio tests.

#include "effect.h"
#include "fx_factory.h"
#include "test_util.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using testutil::kBlock;
using testutil::kRate;
using testutil::SignalGen;
using testutil::firstBad;
using testutil::peakAbs;
using testutil::registry;

namespace {
// Bound for "the effect blew up": generous enough for hot boosts (a drive at
// max adds ~30 dB to a ~0.8 peak input), tiny next to runaway feedback.
constexpr float kLimit = 100.0f;
// Effect::run() crossfade step for a ~10 ms bypass ramp (mirrors chain.h).
constexpr float kFadeStep = float(1.0 / (0.010 * kRate));
}  // namespace

// The big one: every kind x every param at {min, mid, max} (others at default),
// plus an all-defaults pass, processing ~0.5 s of hot sine+noise. Any NaN, inf,
// or sample beyond kLimit fails with the kind/param/setting captured.
TEST_CASE("no NaN/inf/blowup across param extremes", "[dsp][sweep]") {
  const int blocksPerCombo = int(0.5 * kRate) / kBlock;  // ~0.5 s of audio
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
  int combosTested = 0;

  for (const FxKind& kind : registry().kinds()) {
    const size_t nParams = kind.make()->params.size();
    // p == nParams is the all-defaults pass (one setting only).
    for (size_t p = 0; p <= nParams; p++) {
      const int nSettings = (p == nParams) ? 1 : 3;
      for (int setting = 0; setting < nSettings; setting++) {
        auto fx = kind.make();
        REQUIRE(fx != nullptr);
        fx->prepare(kRate, kBlock);

        std::string paramId = "(defaults)";
        float value = 0.0f;
        if (p < nParams) {
          Param& prm = *fx->params[p];
          paramId = prm.id;
          value = setting == 0   ? prm.min
                  : setting == 1 ? prm.min + (prm.max - prm.min) * 0.5f
                                 : prm.max;
          prm.set(value);
        }

        SignalGen gen;
        for (int b = 0; b < blocksPerCombo; b++) {
          gen.fill(L.data(), R.data(), kBlock);
          fx->process(L.data(), R.data(), kBlock);
          const int badL = firstBad(L.data(), kBlock, kLimit);
          const int badR = firstBad(R.data(), kBlock, kLimit);
          if (badL >= 0 || badR >= 0) {
            CAPTURE(kind.type, paramId, setting, value, b, badL, badR);
            REQUIRE(badL < 0);
            REQUIRE(badR < 0);
          }
        }
        combosTested++;
      }
    }
  }
  REQUIRE(combosTested > 0);
}

// Non-tails effects at default settings must go quiet when the input does:
// charge the state with 0.5 s of signal, then feed 1 s of silence and require
// the final 100 ms to sit below -60 dBFS.
TEST_CASE("silence in -> silence out (non-tails, defaults)", "[dsp]") {
  const int chargeBlocks = int(0.5 * kRate) / kBlock;
  const int silentBlocks = int(1.0 * kRate) / kBlock;
  const int tailWindow = int(0.1 * kRate) / kBlock;  // final 100 ms
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);

  for (const FxKind& kind : registry().kinds()) {
    auto fx = kind.make();
    REQUIRE(fx != nullptr);
    if (fx->hasTails()) continue;  // covered by the decay test below
    fx->prepare(kRate, kBlock);

    SignalGen gen;
    for (int b = 0; b < chargeBlocks; b++) {
      gen.fill(L.data(), R.data(), kBlock);
      fx->process(L.data(), R.data(), kBlock);
    }
    float finalPeak = 0.0f;
    for (int b = 0; b < silentBlocks; b++) {
      std::memset(L.data(), 0, sizeof(float) * kBlock);
      std::memset(R.data(), 0, sizeof(float) * kBlock);
      fx->process(L.data(), R.data(), kBlock);
      if (b >= silentBlocks - tailWindow) {
        finalPeak = std::max(finalPeak, peakAbs(L.data(), kBlock));
        finalPeak = std::max(finalPeak, peakAbs(R.data(), kBlock));
      }
    }
    CAPTURE(kind.type, finalPeak);
    CHECK(finalPeak < 1.0e-3f);  // -60 dBFS
  }
}

// Bypass null test through Effect::run(), the entry point the chain uses.
// A settled-bypassed non-tails effect returns early, so the buffer must come
// back BIT-identical. Tails effects stay live while bypassed (input faded to
// zero, dry added back), so a freshly-prepared one must come back ~identical.
TEST_CASE("bypass is a null-op", "[dsp][bypass]") {
  const int blocks = int(0.5 * kRate) / kBlock;
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
  std::vector<float> refL((size_t)kBlock), refR((size_t)kBlock);
  std::vector<float> dryL((size_t)kBlock), dryR((size_t)kBlock);

  for (const FxKind& kind : registry().kinds()) {
    auto fx = kind.make();
    REQUIRE(fx != nullptr);
    fx->prepare(kRate, kBlock);
    fx->enabled.store(false);
    fx->snapFade();

    SignalGen gen;
    float worst = 0.0f;
    bool bitExact = true;
    for (int b = 0; b < blocks; b++) {
      gen.fill(L.data(), R.data(), kBlock);
      std::memcpy(refL.data(), L.data(), sizeof(float) * kBlock);
      std::memcpy(refR.data(), R.data(), sizeof(float) * kBlock);
      fx->run(L.data(), R.data(), kBlock, dryL.data(), dryR.data(), kFadeStep);
      if (std::memcmp(refL.data(), L.data(), sizeof(float) * kBlock) != 0 ||
          std::memcmp(refR.data(), R.data(), sizeof(float) * kBlock) != 0)
        bitExact = false;
      for (int i = 0; i < kBlock; i++) {
        worst = std::max(worst, std::fabs(L[(size_t)i] - refL[(size_t)i]));
        worst = std::max(worst, std::fabs(R[(size_t)i] - refR[(size_t)i]));
      }
    }
    CAPTURE(kind.type, worst);
    if (fx->hasTails())
      CHECK(worst < 1.0e-2f);  // stays live; allow for float noise / hiss
    else
      CHECK(bitExact);         // settled bypass returns before touching audio
  }
}

// Tails effects at default settings: after a short burst, the ring-out must
// actually decay -- any 100 ms window below -60 dBFS within 30 s passes. A
// default patch that never gets there means runaway feedback.
TEST_CASE("tails decay to silence at default settings", "[dsp][tails]") {
  const int burstBlocks = int(0.25 * kRate) / kBlock;
  const int maxSilentBlocks = int(30.0 * kRate) / kBlock;
  const int windowBlocks = int(0.1 * kRate) / kBlock;
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
  int tailsKinds = 0;

  for (const FxKind& kind : registry().kinds()) {
    auto fx = kind.make();
    REQUIRE(fx != nullptr);
    if (!fx->hasTails()) continue;
    tailsKinds++;
    fx->prepare(kRate, kBlock);

    SignalGen gen;
    for (int b = 0; b < burstBlocks; b++) {
      gen.fill(L.data(), R.data(), kBlock);
      fx->process(L.data(), R.data(), kBlock);
    }
    bool decayed = false;
    int quietRun = 0;
    for (int b = 0; b < maxSilentBlocks && !decayed; b++) {
      std::memset(L.data(), 0, sizeof(float) * kBlock);
      std::memset(R.data(), 0, sizeof(float) * kBlock);
      fx->process(L.data(), R.data(), kBlock);
      const float peak =
          std::max(peakAbs(L.data(), kBlock), peakAbs(R.data(), kBlock));
      quietRun = (peak < 1.0e-3f) ? quietRun + 1 : 0;
      if (quietRun >= windowBlocks) decayed = true;
    }
    CAPTURE(kind.type);
    CHECK(decayed);
  }
  REQUIRE(tailsKinds > 0);  // the registry does contain delays/reverbs
}

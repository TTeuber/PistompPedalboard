// effects/comp.h -- optical-style studio compressor, right after the gate.
//
// Hand-rolled (replaces juce::dsp::Compressor, which was hard-knee with
// unlinked channels). Built to live always-on at the front of the chain:
//   * Stereo-linked detector -- one gain for both channels (max of |L|,|R|),
//     so the image never shifts when one side peaks first.
//   * 6 dB soft knee -- compression fades in around the threshold instead of
//     grabbing at it; the threshold marker on the input meter is the knee's
//     midpoint.
//   * Fixed 10 ms attack -- slow enough to let the pick transient through.
//   * Program-dependent release (the optical trick): a ~400 ms average of the
//     gain reduction steers the release time between 80 ms and 700 ms. Brief
//     peaks recover fast (snappy, no lost notes); sustained strumming releases
//     slowly (no pumping). This is what makes an LA-2A feel invisible.
//   * Auto-makeup: derived from Threshold/Ratio assuming guitar peaks around
//     -10 dBFS, so cranking the compression doesn't drop the level and the
//     Makeup knob is freed up for...
//   * Blend -- parallel (NY) compression, the pedal-world killer feature: dry
//     keeps the attack alive, the compressed path carries the sustain. 100% =
//     normal compressor; ~60-70% is the "always on" sweet spot.
//
// Controls stay at three: Threshold (how loud before it clamps), Ratio (how
// hard), Blend (how much of the compressed signal). Threshold/ratio ids are
// unchanged so presets restore and both UIs keep their threshold marker; old
// preset "makeup" values are silently dropped (auto-makeup covers them).
//
// The web UI draws the threshold as a marker line on the input meter and a
// shared gain-reduction bar; we publish the detector's actual per-block max
// gain reduction (not a peak-in/peak-out estimate) lock-free in grDb_.
//
// Everything runs per sample in the log domain: one log10 + one exp per frame,
// allocation-free, real-time safe.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace fx {

class Comp : public Effect {
public:
  Comp() : Effect("comp", "Compressor") {
    thresh_ = addParam("threshold", "Threshold", "dB", -40, 0, -18);
    ratio_  = addParam("ratio",     "Ratio",     ":1", 1,  10, 4);
    blend_  = addParam("blend",     "Blend",     "%",  0, 100, 100);
  }

  void prepare(double sr, int maxBlock) override {
    (void)maxBlock;
    sr_ = sr;
    aAtk_ = onePole(kAttackMs);
    aAvg_ = onePole(kAvgMs);
    gr_ = grAvg_ = 0.0f;
    makeupS_.prepare(sr, 20.0);
    makeupS_.snap(dbToLin(autoMakeupDb()));
    blendS_.prepare(sr, 20.0);
    blendS_.snap(blend_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float T = thresh_->get();
    const float cr = 1.0f - 1.0f / ratio_->get();  // GR per dB over threshold
    const float makeupT = dbToLin(autoMakeupDb());
    const float blendT = blend_->get() / 100.0f;

    // Program-dependent release: the slow GR average picks the time constant.
    // grAvg_ moves over ~400 ms, so a block-rate coefficient is plenty.
    const float sense = std::clamp(grAvg_ / kRelDepthDb, 0.0f, 1.0f);
    const float aRel = onePole(kRelFastMs + (kRelSlowMs - kRelFastMs) * sense);

    float grMax = 0.0f;
    for (int i = 0; i < n; i++) {
      const float inL = L[i], inR = R[i];

      // Linked detector: loudest of the two channels, in dB (-120 dB floor).
      const float d = std::max(std::fabs(inL), std::fabs(inR));
      const float xDb = 20.0f * std::log10(std::max(d, 1e-6f));

      // Static curve: quadratic soft knee, kKneeDb wide, centred on T.
      const float over = xDb - T;
      float grT;
      if (2.0f * over <= -kKneeDb) {
        grT = 0.0f;
      } else if (2.0f * over >= kKneeDb) {
        grT = cr * over;
      } else {
        const float t = over + kKneeDb * 0.5f;
        grT = cr * t * t / (2.0f * kKneeDb);
      }

      // Ballistics on the gain reduction itself: fast-ish grab, program-
      // dependent recovery.
      if (grT > gr_)
        gr_ = grT + aAtk_ * (gr_ - grT);
      else
        gr_ = grT + aRel * (gr_ - grT);
      grAvg_ = gr_ + aAvg_ * (grAvg_ - gr_);
      grMax = std::max(grMax, gr_);

      const float g = std::exp(gr_ * -kDbToNat);  // 10^(-gr/20)
      const float mk = makeupS_.next(makeupT);
      const float bl = blendS_.next(blendT);
      const float wetL = inL * g * mk;
      const float wetR = inR * g * mk;
      L[i] = inL + (wetL - inL) * bl;
      R[i] = inR + (wetR - inR) * bl;
    }

    // Peak-hold: keep the largest reduction since the meter last read-and-cleared
    // (same lock-free max as PedalControls::inPeak). A disabled comp never writes,
    // so its held value decays to 0 on the next web read.
    float cur = grDb_.load(std::memory_order_relaxed);
    while (grMax > cur &&
           !grDb_.compare_exchange_weak(cur, grMax, std::memory_order_relaxed)) {}
    // Mirror into the device-UI peak-hold (separate consumer; see gate.h).
    float curD = grDbDev_.load(std::memory_order_relaxed);
    while (grMax > curD &&
           !grDbDev_.compare_exchange_weak(curD, grMax, std::memory_order_relaxed)) {}
  }

  // Largest gain reduction (dB) since the last call; reading clears the peak hold
  // so a disabled comp decays to 0. Called by the web meter (web_server.cpp).
  float takeGrDb() noexcept { return grDb_.exchange(0.0f, std::memory_order_relaxed); }
  // Same, for the on-device gain-reduction meter (ui_controller.cpp).
  float takeGrDbDev() noexcept { return grDbDev_.exchange(0.0f, std::memory_order_relaxed); }

private:
  static constexpr float kKneeDb = 6.0f;      // soft-knee width
  static constexpr float kAttackMs = 10.0f;   // fixed, lets the pick through
  static constexpr float kRelFastMs = 80.0f;  // release for brief peaks...
  static constexpr float kRelSlowMs = 700.0f; // ...and for sustained squash
  static constexpr float kRelDepthDb = 6.0f;  // avg GR that means "sustained"
  static constexpr float kAvgMs = 400.0f;     // GR history for release steering
  static constexpr float kNomDb = -10.0f;     // assumed guitar peak level
  static constexpr float kDbToNat = 0.11512925f;  // ln(10)/20

  // Makeup that restores a kNomDb-peaking signal to its pre-compression level.
  // Tracks Threshold/Ratio so the comp stays roughly unity-loudness however
  // it's dialed. Matches the 5-7 dB the old rigs set by hand at T=-18, R=4.
  float autoMakeupDb() const noexcept {
    const float cr = 1.0f - 1.0f / ratio_->get();
    return std::min(std::max(0.0f, kNomDb - thresh_->get()) * cr, 24.0f);
  }

  float onePole(float ms) const noexcept {
    return std::exp(-1.0f / (0.001f * ms * (float)sr_));
  }
  static float dbToLin(float db) noexcept { return std::exp(db * kDbToNat); }

  double sr_ = 48000.0;
  float aAtk_ = 0.0f, aAvg_ = 0.0f;
  float gr_ = 0.0f;     // smoothed gain reduction, dB >= 0 (audio thread only)
  float grAvg_ = 0.0f;  // slow GR average steering the release time
  Smoother makeupS_, blendS_;
  Param *thresh_, *ratio_, *blend_;
  std::atomic<float> grDb_{0.0f};
  std::atomic<float> grDbDev_{0.0f};
};

}  // namespace fx

// effects/rotary.h -- rotary speaker (Leslie) simulation.
//
// The signal is mono-summed, gently tanh-driven (the pushed-tube growl), and
// split ~800 Hz by a complementary one-pole crossover into a treble HORN and a
// bass DRUM rotor. Each rotor is one spinning source rendered three ways from
// a single sin/cos pair per sample:
//   * Doppler -- a modulated delay tap (the pitch wobble of approach/recede),
//   * AM      -- level dips as the source faces away,
//   * panning -- the source swings across the stereo mics.
// Speed toggles slow/fast TARGET rates; the actual rates chase them through
// long smoothers with different inertias (horn ~0.8 s, drum ~3.5 s), so the
// horn audibly arrives at fast before the drum -- the Leslie spin-up tell.
// Balance is the drum<->horn mic blend; Mix trims the whole speaker in (at
// 100 it IS the amp's speaker). No cabinet IR -- scope-bound to one header.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "mod_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {

class Rotary : public Effect {
public:
  Rotary() : Effect("rotary", "Rotary") {
    speed_ = addParam("speed",   "Speed",   "",  0, 1,   0);  // enum: Slow/Fast
    drive_ = addParam("drive",   "Drive",   "%", 0, 100, 15);
    bal_   = addParam("balance", "Balance", "%", 0, 100, 55);
    mix_   = addParam("mix",     "Mix",     "%", 0, 100, 100);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    const int maxSamps = (int)std::ceil(sr * 0.003);
    horn_.prepare(mono, maxSamps);
    drum_.prepare(mono, maxSamps);
    xover_.setCutoff(kXoverHz, sr);
    xover_.reset();
    hornPhase_ = 0.0f;
    drumPhase_ = 0.3f;  // start offset so the rotors never spin in lockstep
    // Rotor inertia: the rate chases its slow/fast target through these.
    hornRateS_.prepare(sr, kHornInertiaMs);
    hornRateS_.snap(speed_->get() > 0.5f ? kHornFast : kHornSlow);
    drumRateS_.prepare(sr, kDrumInertiaMs);
    drumRateS_.snap(speed_->get() > 0.5f ? kDrumFast : kDrumSlow);
    driveS_.prepare(sr, 20.0);
    driveS_.snap(driveGain());
    balS_.prepare(sr, 20.0);
    balS_.snap(bal_->get() / 100.0f);
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
    hornCentre_ = (float)(kHornCentreMs * 0.001 * sr);
    hornDepth_ = (float)(kHornDepthMs * 0.001 * sr);
    drumCentre_ = (float)(kDrumCentreMs * 0.001 * sr);
    drumDepth_ = (float)(kDrumDepthMs * 0.001 * sr);
  }

  void process(float* L, float* R, int n) noexcept override {
    const bool fast = speed_->get() > 0.5f;
    const float hornT = fast ? kHornFast : kHornSlow;
    const float drumT = fast ? kDrumFast : kDrumSlow;
    const float driveT = driveGain();
    const float balT = bal_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;

    for (int i = 0; i < n; i++) {
      // The rates ARE the smoothers' outputs -- inertia by construction.
      const float hornHz = hornRateS_.next(hornT);
      const float drumHz = drumRateS_.next(drumT);
      hornPhase_ += hornHz / (float)sr_;
      if (hornPhase_ >= 1.0f) hornPhase_ -= 1.0f;
      drumPhase_ += drumHz / (float)sr_;
      if (drumPhase_ >= 1.0f) drumPhase_ -= 1.0f;
      const float g = driveS_.next(driveT);
      const float bal = balS_.next(balT);
      const float mix = mixS_.next(mixT);

      const float inL = L[i], inR = R[i];
      // Mono in, drive, split.
      float m = 0.5f * (inL + inR);
      m = std::tanh(g * m) / g;  // unity small-signal slope at any drive
      const float low = xover_.process(m);
      const float high = m - low;

      // Horn: one sin/cos pair renders Doppler + AM + pan.
      const float hs = std::sin(2.0f * (float)M_PI * hornPhase_);
      const float hc = std::cos(2.0f * (float)M_PI * hornPhase_);
      horn_.write(high);
      float h = horn_.read(hornCentre_ + hornDepth_ * hs);
      h *= 1.0f - kHornAm * (0.5f + 0.5f * hc);
      const float hgL = 0.5f + kHornPan * 0.5f * hs;

      // Drum: same recipe, slower/deeper/narrower.
      const float ds = std::sin(2.0f * (float)M_PI * drumPhase_);
      const float dc = std::cos(2.0f * (float)M_PI * drumPhase_);
      drum_.write(low);
      float d = drum_.read(drumCentre_ + drumDepth_ * ds);
      d *= 1.0f - kDrumAm * (0.5f + 0.5f * dc);
      const float dgL = 0.5f + kDrumPan * 0.5f * ds;

      // Equal-power mic balance (unity at centre), x2 makeup for the pan split.
      const float hG = std::sqrt(2.0f * bal);
      const float dG = std::sqrt(2.0f * (1.0f - bal));
      const float wetL = 2.0f * (h * hgL * hG + d * dgL * dG);
      const float wetR = 2.0f * (h * (1.0f - hgL) * hG + d * (1.0f - dgL) * dG);

      L[i] = inL * (1.0f - mix) + wetL * mix;
      R[i] = inR * (1.0f - mix) + wetR * mix;
    }
  }

private:
  float driveGain() const noexcept {
    return 1.0f + drive_->get() / 100.0f * 9.0f;
  }

  static constexpr double kXoverHz = 800.0;
  // Slow/fast rotor rates (Hz) and spin-up/down inertias -- horn light and
  // quick, drum heavy and slow. The stagger is the Leslie signature.
  static constexpr float kHornSlow = 0.8f, kHornFast = 6.8f;
  static constexpr float kDrumSlow = 0.7f, kDrumFast = 5.7f;
  static constexpr double kHornInertiaMs = 800.0, kDrumInertiaMs = 3500.0;
  // Doppler tap centre/swing (ms), AM depth, pan swing per rotor.
  static constexpr double kHornCentreMs = 1.0, kHornDepthMs = 0.9;
  static constexpr double kDrumCentreMs = 0.5, kDrumDepthMs = 0.4;
  static constexpr float kHornAm = 0.35f, kDrumAm = 0.15f;
  static constexpr float kHornPan = 0.8f, kDrumPan = 0.4f;

  double sr_ = 48000.0;
  md::ModDelay horn_, drum_;
  OnePole xover_;
  float hornPhase_ = 0.0f, drumPhase_ = 0.3f;
  float hornCentre_ = 0, hornDepth_ = 0, drumCentre_ = 0, drumDepth_ = 0;
  Smoother hornRateS_, drumRateS_, driveS_, balS_, mixS_;
  Param *speed_, *drive_, *bal_, *mix_;
};

}  // namespace fx

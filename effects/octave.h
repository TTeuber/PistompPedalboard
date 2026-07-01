// effects/octave.h -- analog-style mono octave (sub-octave + octave-up).
//
// No FFT / phase-vocoder -- the classic analog trick, so it's cheap and RT-safe
// but MONOPHONIC (tracks one note at a time, vintage character a la Boss OC-2 /
// Tycobrahe Octavia):
//   * Octave UP   = full-wave rectify |x| (doubles the frequency), then DC-block.
//   * Octave DOWN = divide the frequency by two with a sign flip-flop. The naive
//     "flip on every zero crossing" is what makes a cheap octaver jump octaves:
//     harmonics and noise add extra crossings, so the divider mis-counts. We fix
//     that two ways -- a steep tracking LOW-PASS first isolates the fundamental,
//     and a SCHMITT TRIGGER (must dip below -hyst, then rise above +hyst) gives
//     exactly one clean flip per cycle. The hysteresis scales with the envelope
//     so it stays note-relative.
// Dry, Sub and Up blend the three voices; Tone rolls off the synthetic voices.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <algorithm>
#include <cmath>

namespace fx {

class Octave : public Effect {
public:
  Octave() : Effect("octave", "Octave") {
    sub_  = addParam("sub",  "Sub",  "%", 0, 100, 60);  // octave down
    up_   = addParam("up",   "Up",   "%", 0, 100, 0);   // octave up
    dry_  = addParam("dry",  "Dry",  "%", 0, 100, 80);
    tone_ = addParam("tone", "Tone", "%", 0, 100, 50);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    track1_.setLowpass(400.0, sr);  // two cascaded poles -> steeper fundamental
    track2_.setLowpass(400.0, sr);  // isolation so harmonics can't add crossings
    track1_.reset();
    track2_.reset();
    subLp_.reset();
    upLp_.reset();
    envLp_.reset();
    dcPrev_ = dcOut_ = 0.0f;
    flip_ = 1.0f;
    armed_ = false;
    subS_.prepare(sr, 20.0);
    upS_.prepare(sr, 20.0);
    dryS_.prepare(sr, 20.0);
    subS_.snap(sub_->get() / 100.0f);
    upS_.snap(up_->get() / 100.0f);
    dryS_.snap(dry_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float subT = sub_->get() / 100.0f;
    const float upT = up_->get() / 100.0f;
    const float dryT = dry_->get() / 100.0f;
    // Tone rolls the synthetic voices ~800 Hz (dark) .. ~6 kHz (open).
    const double fc = 800.0 * std::pow(6000.0 / 800.0, tone_->get() / 100.0);
    subLp_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    upLp_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    envLp_.setCutoff(20.0, sr_);  // slow amplitude follower

    for (int i = 0; i < n; i++) {
      // Voice mixes smoothed per sample so swept knobs glide instead of step.
      const float sub = subS_.next(subT);
      const float up = upS_.next(upT);
      const float dry = dryS_.next(dryT);
      float m = 0.5f * (L[i] + R[i]);            // collapse to mono for tracking
      float env = envLp_.process(std::fabs(m));  // loudness of the synth voices

      // Octave up: rectification doubles the pitch; remove the DC it adds.
      float rect = std::fabs(m);
      float dc = rect - dcPrev_ + 0.995f * dcOut_;  // one-pole DC blocker
      dcPrev_ = rect;
      dcOut_ = dc;
      float upV = upLp_.process(dc);

      // Octave down: track the fundamental, then a Schmitt-trigger divide-by-two.
      float trk = track2_.process(track1_.process(m));
      float hyst = 0.15f * env + 1.0e-4f;          // note-relative dead zone
      if (!armed_ && trk < -hyst) armed_ = true;   // dipped below lower rail
      if (armed_ && trk > hyst) {                  // ...then rose above upper rail
        flip_ = -flip_;                            // one flip per input cycle
        armed_ = false;
      }
      float subV = subLp_.process(flip_ * env);    // voiced square, smoothed

      // Makeup (~2x) since the synthetic voices sit well below the dry level.
      float wet = subV * sub * 2.0f + upV * up * 2.0f;
      L[i] = L[i] * dry + wet;
      R[i] = R[i] * dry + wet;
    }
  }

private:
  double sr_ = 48000.0;
  Biquad track1_, track2_;              // cascaded LP: isolate the fundamental
  OnePole subLp_, upLp_, envLp_;
  float dcPrev_ = 0.0f, dcOut_ = 0.0f;  // octave-up DC blocker state
  float flip_ = 1.0f;                   // octave-down divider sign
  bool armed_ = false;                  // Schmitt-trigger arm state
  Smoother subS_, upS_, dryS_;
  Param *sub_, *up_, *dry_, *tone_;
};

}  // namespace fx

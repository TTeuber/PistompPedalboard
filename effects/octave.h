// effects/octave.h -- octave pedal with two engines, picked by the Mode knob:
//
//   MONO (vintage, the default) -- the classic analog trick, cheap and RT-safe
//   but tracks one note at a time (a la Boss OC-2 / Tycobrahe Octavia):
//     * Octave UP   = full-wave rectify the tracked fundamental (doubles the
//       frequency), then DC-block. Rectifying the *filtered* signal (not the
//       raw input) keeps intermodulation hash off real guitar.
//     * Octave DOWN = divide the frequency by two with a sign flip-flop driven
//       by a Schmitt trigger (must dip below -hyst, then rise above +hyst) so
//       harmonics/noise can't add extra crossings. The hysteresis is scaled by
//       the envelope of the FILTERED signal -- the same signal it's compared
//       against -- so high notes attenuated by the tracking filter still flip.
//     * The tracking lowpass is ADAPTIVE: each Schmitt flip is one input cycle,
//       so samples-between-flips measures the period directly. The cutoff
//       follows 1.4x the detected fundamental (validated, glided), keeping the
//       fundamental in the passband while the 2nd harmonic sits ~12 dB down
//       the 4th-order cascade. A stall-relax rule reopens the filter after an
//       upward note jump; silence resets it to a neutral default.
//
//   POLY -- granular dual-tap pitch shifters (rv::PitchShift, the shimmer
//   engine): chord-friendly octave up + down at the cost of a little grain
//   warble and ~20-35 ms of wet-only latency. No tracking at all.
//
// Dry, Sub and Up blend the voices in both modes; Tone rolls off the synthetic
// voices. Switching Mode fades the wet out, resets the incoming engine, and
// fades back in (~8 ms each way) so it never clicks.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "reverb_dsp.h"

#include <juce_dsp/juce_dsp.h>

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
    mode_ = addParam("mode", "Mode", "",  0, 1,   0);   // enum: 0 mono, 1 poly
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    trkFc_ = trkTargetHz_ = kTrkDefault;
    track1_.setLowpass(trkFc_, sr);
    track2_.setLowpass(trkFc_, sr);
    track1_.reset();
    track2_.reset();
    subLp_.reset();
    upLp_.reset();
    envLp_.reset();
    envFLp_.reset();
    dcPrev_ = dcOut_ = 0.0f;
    flip_ = 1.0f;
    armed_ = false;
    envRaw_ = 0.0f;
    sinceFlip_ = 0;
    pendingInterval_ = 0;
    quietSamps_ = 0;

    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    psUp_.prepare(mono, kUpWinMs, sr);  psUp_.setRatio(2.0f);   // +12
    psDn_.prepare(mono, kDnWinMs, sr);  psDn_.setRatio(0.5f);   // -12

    activeMode_ = (int)(mode_->get() + 0.5f);
    subS_.prepare(sr, 20.0);
    upS_.prepare(sr, 20.0);
    dryS_.prepare(sr, 20.0);
    subS_.snap(sub_->get() / 100.0f);
    upS_.snap(up_->get() / 100.0f);
    dryS_.snap(dry_->get() / 100.0f);
    modeFadeS_.prepare(sr, kModeFadeMs);
    modeFadeS_.snap(1.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float subT = sub_->get() / 100.0f;
    const float upT = up_->get() / 100.0f;
    const float dryT = dry_->get() / 100.0f;
    // Tone rolls the synthetic voices ~800 Hz (dark) .. ~6 kHz (open).
    const double fc = 800.0 * std::pow(6000.0 / 800.0, tone_->get() / 100.0);
    subLp_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    upLp_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    envLp_.setCutoff(20.0, sr_);   // slow amplitude follower (raw input)
    envFLp_.setCutoff(20.0, sr_);  // ditto for the tracked/filtered signal

    // Mode swap: fade the wet to zero, reset the incoming engine, fade back.
    // The knob Smoothers can't cover an algorithm swap -- this can.
    const int mode = (int)(mode_->get() + 0.5f);
    if (mode != activeMode_ && modeFadeS_.y < 0.01f) {
      activeMode_ = mode;
      if (mode == 1) {
        psUp_.reset();
        psDn_.reset();
      } else {
        track1_.reset(); track2_.reset(); envFLp_.reset();
        dcPrev_ = dcOut_ = 0.0f;
        flip_ = 1.0f; armed_ = false;
        sinceFlip_ = 0; pendingInterval_ = 0;
        trkFc_ = trkTargetHz_ = kTrkDefault;
        track1_.setLowpass(trkFc_, sr_);
        track2_.setLowpass(trkFc_, sr_);
      }
    }
    const float fadeT = (mode == activeMode_) ? 1.0f : 0.0f;

    if (activeMode_ == 1) processPoly(L, R, n, subT, upT, dryT, fadeT);
    else                  processMono(L, R, n, subT, upT, dryT, fadeT);
  }

private:
  void processPoly(float* L, float* R, int n, float subT, float upT,
                   float dryT, float fadeT) noexcept {
    for (int i = 0; i < n; i++) {
      const float sub = subS_.next(subT);
      const float up = upS_.next(upT);
      const float dry = dryS_.next(dryT);
      const float m = 0.5f * (L[i] + R[i]);  // guitar is effectively mono
      // Shifted voices are ~unity level, so no makeup here.
      const float upV = upLp_.process(psUp_.process(m));
      const float dnV = subLp_.process(psDn_.process(m));
      const float wet = (dnV * sub + upV * up) * modeFadeS_.next(fadeT);
      L[i] = L[i] * dry + wet;
      R[i] = R[i] * dry + wet;
    }
  }

  void processMono(float* L, float* R, int n, float subT, float upT,
                   float dryT, float fadeT) noexcept {
    // Stall relax: playing but no flips for >50 ms means the note jumped up
    // and its fundamental is buried below the (now too low) cutoff -- open the
    // target until flips resume (doubles every ~100 ms of wall time).
    if (envRaw_ > kGate && sinceFlip_ > (int)(kStallSec * sr_))
      trkTargetHz_ = std::min(
          trkTargetHz_ * std::exp2((float)n / (float)(kStallDoubleSec * sr_)),
          kTrkMax);
    // Silence: reset to a neutral filter so the next note starts fresh. The
    // divider never free-runs on silence -- the hysteresis floor sees to that.
    quietSamps_ = envRaw_ < kHystFloor ? quietSamps_ + n : 0;
    if (quietSamps_ > (int)(kSilenceSec * sr_)) {
      trkTargetHz_ = kTrkDefault;
      armed_ = false;
      pendingInterval_ = 0;
    }
    // Glide the live cutoff toward the target in the log domain (~30 ms), then
    // retune the cascade once per block (RBJ recompute is trivial at block rate).
    const float aBlk =
        std::exp(-(float)n / (float)(0.001 * kTrkGlideMs * sr_));
    trkFc_ = trkTargetHz_ * std::pow(trkFc_ / trkTargetHz_, aBlk);
    track1_.setLowpass(trkFc_, sr_);
    track2_.setLowpass(trkFc_, sr_);

    for (int i = 0; i < n; i++) {
      // Voice mixes smoothed per sample so swept knobs glide instead of step.
      const float sub = subS_.next(subT);
      const float up = upS_.next(upT);
      const float dry = dryS_.next(dryT);
      float m = 0.5f * (L[i] + R[i]);            // collapse to mono for tracking
      envRaw_ = envLp_.process(std::fabs(m));    // raw loudness (gate/stall)

      // Track the fundamental; everything downstream works on this signal.
      float trk = track2_.process(track1_.process(m));
      float envF = envFLp_.process(std::fabs(trk));  // filtered-signal envelope

      // Octave up: rectifying the near-sinusoidal fundamental doubles the
      // pitch cleanly; remove the DC that rectification adds.
      float rect = std::fabs(trk);
      float dc = rect - dcPrev_ + 0.995f * dcOut_;  // one-pole DC blocker
      dcPrev_ = rect;
      dcOut_ = dc;
      float upV = upLp_.process(dc);

      // Octave down: Schmitt-trigger divide-by-two. Hysteresis is relative to
      // the FILTERED envelope, so it self-normalizes with the tracking filter.
      float hyst = kHystRatio * envF + kHystFloor;
      if (sinceFlip_ < (1 << 30)) sinceFlip_++;
      if (!armed_ && trk < -hyst) armed_ = true;   // dipped below lower rail
      if (armed_ && trk > hyst) {                  // ...then rose above upper rail
        flip_ = -flip_;                            // one flip per input cycle
        armed_ = false;
        // One flip per cycle, so samples-between-flips IS the period. Adapt
        // the tracking cutoff to it -- but only when two intervals in a row
        // agree within 25% (rejects pick-attack chaos and one-off harmonics).
        const int interval = sinceFlip_;
        sinceFlip_ = 0;
        const float f0 = (float)sr_ / (float)interval;
        if (f0 >= kF0Min && f0 <= kF0Max) {
          if (pendingInterval_ > 0 &&
              std::fabs((float)(interval - pendingInterval_)) <
                  0.25f * (float)pendingInterval_)
            trkTargetHz_ = std::clamp(kTrkMult * f0, kTrkMin, kTrkMax);
          pendingInterval_ = interval;
        } else {
          pendingInterval_ = 0;
        }
      }
      float subV = subLp_.process(flip_ * envF);   // voiced square, smoothed

      // Makeup: the filtered envelope runs well below the raw level. Tuned by
      // ear against the dry voice -- adjust here if the balance is off.
      float wet = (subV * sub * kSubMakeup + upV * up * kUpMakeup) *
                  modeFadeS_.next(fadeT);
      L[i] = L[i] * dry + wet;
      R[i] = R[i] * dry + wet;
    }
  }

  // Tracking-filter bounds and behavior (mono mode).
  static constexpr float kTrkMin = 70.0f, kTrkMax = 1000.0f;
  static constexpr float kTrkDefault = 250.0f;  // neutral mid-guitar startup
  static constexpr float kTrkMult = 1.4f;       // cutoff = 1.4x detected f0
  static constexpr double kTrkGlideMs = 30.0;
  static constexpr float kF0Min = 40.0f, kF0Max = 1200.0f;  // sane guitar f0
  static constexpr float kHystRatio = 0.15f;
  static constexpr float kHystFloor = 1.0e-4f;
  static constexpr float kGate = 2.0e-4f;       // "someone is playing" level
  static constexpr double kStallSec = 0.05;
  static constexpr double kStallDoubleSec = 0.10;
  static constexpr double kSilenceSec = 0.10;
  // Voice levels (tune by ear).
  static constexpr float kSubMakeup = 3.0f, kUpMakeup = 4.0f;
  // Poly grain windows: down wants the longer window (grain rate ~7 Hz rides
  // under low-string periods); up at 40 ms cycles at 25 Hz, smooth enough.
  static constexpr float kUpWinMs = 40.0f, kDnWinMs = 70.0f;
  static constexpr double kModeFadeMs = 8.0;

  double sr_ = 48000.0;
  Biquad track1_, track2_;              // cascaded LP: isolate the fundamental
  OnePole subLp_, upLp_, envLp_, envFLp_;
  float dcPrev_ = 0.0f, dcOut_ = 0.0f;  // octave-up DC blocker state
  float flip_ = 1.0f;                   // octave-down divider sign
  bool armed_ = false;                  // Schmitt-trigger arm state
  float envRaw_ = 0.0f;                 // last raw envelope (block-rate checks)
  float trkFc_ = kTrkDefault, trkTargetHz_ = kTrkDefault;
  int sinceFlip_ = 0;                   // samples since the divider flipped
  int pendingInterval_ = 0;             // last flip interval awaiting agreement
  int quietSamps_ = 0;
  rv::PitchShift psUp_, psDn_;          // poly voices
  int activeMode_ = 0;                  // engine actually running (vs mode_)
  Smoother subS_, upS_, dryS_, modeFadeS_;
  Param *sub_, *up_, *dry_, *tone_, *mode_;
};

}  // namespace fx

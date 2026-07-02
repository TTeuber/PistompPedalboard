// effects/ambient_delay.h -- Ambient: the pad machine. A delay whose feedback
// loop smears -- each circulation passes through a pair of allpass diffusers
// (rv::Allpass, the reverbs' diffusion trick), per-band damping, and a gentle
// cross-channel rotation, so the first repeat is a soft echo, the third is a
// wash, and by the sixth the repeats have stopped being events and become a
// cloud. Halfway to a reverb, but it stays a DELAY: tempo-syncable repeat
// spacing (the diffusers' bulk delay is subtracted from the main tap so the
// synced spacing stays honest) and a real feedback knob.
//
// The rotation is orthogonal (energy-preserving) and allpasses are lossless,
// so the FREEZE showcase works exactly: input muted, loop gain unity, damping
// faded out -- the held slice circulates forever while the diffusers and
// their slowly-modulated taps keep REARRANGING it. A frozen chord doesn't
// loop; it slowly evolves into a shifting pad. dly::softLimit guards the
// loop; feedback at 100% (pre-rotation unity) blooms into a self-sustaining
// wash bounded by it.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "../tempo.h"
#include "delay_dsp.h"
#include "reverb_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class AmbientDelay : public Effect {
public:
  AmbientDelay() : Effect("ambient", "Ambient") {
    time_    = addParam("time",     "Time",     "ms", 50, 2000, 600);
    fb_      = addParam("feedback", "Feedback", "%",  0,  100,  55);
    mix_     = addParam("mix",      "Mix",      "%",  0,  100,  45);
    diffuse_ = addParam("diffuse",  "Diffuse",  "%",  0,  100,  60);
    damping_ = addParam("damping",  "Damping",  "%",  0,  100,  45);
    width_   = addParam("width",    "Width",    "%",  0,  100,  100);
    // Default Div = whole note: ambient swells breathe at the bar.
    sync_    = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_     = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 0);  // enum
    freeze_  = addParam("freeze",   "Freeze",   "",   0, 1, 0);              // toggle
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int maxBlock) override {
    sr_ = sr;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)maxBlock;
    spec.numChannels = 2;
    line_.prepare(spec);
    line_.setMaximumDelayInSamples((int)std::ceil(sr * (kMaxMs / 1000.0)) + 8);
    line_.reset();

    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    for (int c = 0; c < 2; c++) {
      apTotalSamps_[c] = 0.0f;
      for (int a = 0; a < kNAp; a++) {
        // +2 ms guard for the diffusion-tap wobble.
        ap_[c][a].prepare(mono, (int)std::ceil(sr * ((kApMs[c][a] + 2.0) / 1000.0)));
        apSamps_[c][a] = (float)(kApMs[c][a] * 0.001 * sr);
        apTotalSamps_[c] += apSamps_[c][a];
      }
      damp_[c].reset();
      hp_[c].setCutoff(120.0, sr);
      hp_[c].reset();
    }
    // Slow, incommensurate wobbles on the first diffuser of each side: breaks
    // up metallic build at high feedback and keeps a frozen loop evolving.
    apLfo_[0].setRate(0.29f, sr);
    apLfo_[1].setRate(0.37f, sr);
    apLfo_[1].setPhase(0.5f);
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    widthS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    widthS_.snap(width_->get() / 100.0f);
    frz_.prepare(sr);
    glide_.prepare(sr);
    glide_.snap(targetSamps());
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const float widthT = width_->get() / 100.0f;
    const bool frozen = freeze_->get() > 0.5f;
    // Diffusion strength: g 0 (discrete repeats) .. 0.72 (dense smear).
    const float apG = 0.72f * diffuse_->get() / 100.0f;
    for (int c = 0; c < 2; c++)
      for (int a = 0; a < kNAp; a++) ap_[c][a].g = apG;
    // Damping like the Hall: 18 kHz (open) down to ~0.9 kHz (dark).
    const double dampFc =
        18000.0 * std::pow(10.0, -1.3 * (damping_->get() / 100.0));
    damp_[0].setCutoff(std::min(dampFc, sr_ * 0.45), sr_);
    damp_[1].setCutoff(std::min(dampFc, sr_ * 0.45), sr_);
    const float wobSamps = 0.4f * 0.001f * (float)sr_;

    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float frz = frz_.next(frozen);
      const float d = glide_.next(dTarget);
      const float loopGain = fb * (1.0f - frz) + frz;

      const float inL = L[i], inR = R[i];
      // The diffusers add their bulk delay inside the loop, so the main tap is
      // shortened by it -- the audible repeat spacing equals Time (sync stays
      // honest). Clamped so tiny Times still leave a real tap.
      const float tapL = line_.popSample(
          0, std::max(kMinTapSamps * (float)sr_, d - apTotalSamps_[0]));
      const float tapR = line_.popSample(
          1, std::max(kMinTapSamps * (float)sr_, d - apTotalSamps_[1]));

      // Loop path: diffuse (lossless, stays during freeze), then damp + thin
      // (lossy, crossfades out during freeze).
      float sL = tapL, sR = tapR;
      const float wobL = dly::Lfo::sine(apLfo_[0].next()) * wobSamps;
      const float wobR = dly::Lfo::sine(apLfo_[1].next()) * wobSamps;
      sL = ap_[0][0].process(sL, apSamps_[0][0] + wobL);
      sL = ap_[0][1].process(sL, apSamps_[0][1]);
      sR = ap_[1][0].process(sR, apSamps_[1][0] + wobR);
      sR = ap_[1][1].process(sR, apSamps_[1][1]);
      const float cL = hp_[0].process(damp_[0].process(sL));
      const float cR = hp_[1].process(damp_[1].process(sR));
      float fL = (cL * (1.0f - frz) + sL * frz) * loopGain;
      float fR = (cR * (1.0f - frz) + sR * frz) * loopGain;

      // Orthogonal cross-rotation: repeats bloom across the field, and the
      // loop stays exactly unity-energy when frozen.
      const float wL = kCos * fL + kSin * fR;
      const float wR = -kSin * fL + kCos * fR;
      line_.pushSample(0, dly::softLimit(inL * (1.0f - frz) + wL));
      line_.pushSample(1, dly::softLimit(inR * (1.0f - frz) + wR));

      // Width: collapse the wet toward mono as width -> 0.
      const float w = widthS_.next(widthT);
      const float mid = 0.5f * (tapL + tapR);
      const float side = 0.5f * (tapL - tapR) * w;
      L[i] = inL * (1.0f - mix) + (mid + side) * mix;
      R[i] = inR * (1.0f - mix) + (mid - side) * mix;
    }
  }

private:
  float targetSamps() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get());
    return std::clamp(ms, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 2000.0f;
  static constexpr float kMinTapSamps = 0.010f;  // 10 ms floor (x sr in use)
  static constexpr int kNAp = 2;
  // Mutually incommensurate, different per side for decorrelation.
  static constexpr double kApMs[2][kNAp] = {{11.9, 7.3}, {13.1, 8.9}};
  // 20 degree rotation.
  static constexpr float kCos = 0.9397f, kSin = 0.3420f;

  double sr_ = 48000.0;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>
      line_;
  rv::Allpass ap_[2][kNAp];
  OnePole damp_[2];
  dly::HighPass1 hp_[2];
  dly::Lfo apLfo_[2];
  float apSamps_[2][kNAp] = {{0}};
  float apTotalSamps_[2] = {0};
  Smoother fbS_, mixS_, widthS_;
  dly::Freeze frz_;
  dly::GlideValue glide_;
  Param *time_, *fb_, *mix_, *diffuse_, *damping_, *width_, *sync_, *div_,
      *freeze_;
};

}  // namespace fx

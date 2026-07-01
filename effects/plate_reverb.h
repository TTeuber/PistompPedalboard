// effects/plate_reverb.h -- classic bright, dense plate (Dattorro topology).
//
// Jon Dattorro's 1997 plate reverb: an input bandwidth filter and four
// diffusion allpasses feed a cross-coupled "figure-8" tank of two allpass +
// delay + damping branches. The tank builds an extremely dense, bright tail
// almost instantly (the plate signature), and stereo outputs are summed from
// several interior taps for a wide, decorrelated wash. Delay lengths are the
// paper's values (specified at 29761 Hz) scaled to the running sample rate.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "reverb_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {

class Plate : public Effect {
public:
  Plate() : Effect("plate", "Plate") {
    mix_       = addParam("mix",       "Mix",       "%", 0, 100, 30);
    decay_     = addParam("decay",     "Decay",     "%", 0, 100, 60);
    predelay_  = addParam("predelay",  "Pre-Delay", "ms",0, 250, 10);
    damping_   = addParam("damping",   "Damping",   "%", 0, 100, 40);
    bandwidth_ = addParam("bandwidth", "Bandwidth", "%", 0, 100, 80);
    diffusion_ = addParam("diffusion", "Diffusion", "%", 0, 100, 100);
    width_     = addParam("width",     "Width",     "%", 0, 100, 100);
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int) override {
    sr_ = sr;
    scale_ = (float)(sr / 29761.0);  // Dattorro's constants are @29761 Hz

    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;

    pre_.prepare(mono, (int)std::ceil(sr * 0.30));
    for (int i = 0; i < 4; i++)
      in_[i].prepare(mono, maxSamps(kInMs[i]));
    dd1L_.prepare(mono, maxSamps(kDd1L) + kModSamps + 4);
    dd1R_.prepare(mono, maxSamps(kDd1R) + kModSamps + 4);
    dd2L_.prepare(mono, maxSamps(kDd2L));
    dd2R_.prepare(mono, maxSamps(kDd2R));
    delA_.prepare(mono, maxSamps(kDelA));
    delB_.prepare(mono, maxSamps(kDelB));
    delC_.prepare(mono, maxSamps(kDelC));
    delD_.prepare(mono, maxSamps(kDelD));

    bw_.reset();
    dampL_.reset();
    dampR_.reset();
    nodeC_ = nodeD_ = 0.0f;
    modPhase_ = 0.0f;

    mixS_.prepare(sr, 20.0);   mixS_.snap(mix_->get() / 100.0f);
    widthS_.prepare(sr, 20.0); widthS_.snap(width_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float decay = 0.30f + (decay_->get() / 100.0f) * 0.62f;   // 0.30..0.92
    const double dampFc =
        18000.0 * std::pow(10.0, -1.3 * (damping_->get() / 100.0));
    const double bwFc = 2000.0 + (bandwidth_->get() / 100.0) * 17000.0;
    const float dif = diffusion_->get() / 100.0f;
    const float preSamps = predelay_->get() * 0.001f * (float)sr_;

    bw_.setCutoff(std::min(bwFc, sr_ * 0.49), sr_);
    dampL_.setCutoff(std::min(dampFc, sr_ * 0.49), sr_);
    dampR_.setCutoff(std::min(dampFc, sr_ * 0.49), sr_);
    for (int i = 0; i < 4; i++) in_[i].g = kInG[i] * dif;
    dd1L_.g = dd1R_.g = 0.70f * dif;
    dd2L_.g = dd2R_.g = 0.50f * dif;

    const float mixT = mix_->get() / 100.0f;
    const float widthT = width_->get() / 100.0f;
    const float modInc = 0.9f / (float)sr_;  // ~0.9 Hz tank wobble

    for (int i = 0; i < n; i++) {
      const float dryL = L[i], dryR = R[i];

      // Input conditioning: pre-delay, bandwidth LP, four diffusers.
      pre_.write((dryL + dryR) * 0.5f);
      float x = preSamps > 1.0f ? pre_.read(preSamps) : (dryL + dryR) * 0.5f;
      x = bw_.process(x);
      for (int a = 0; a < 4; a++) x = in_[a].process(x, samps(kInMs[a]));

      // Slow modulation of the first tank allpass in each half (breaks ringing).
      modPhase_ += modInc;
      if (modPhase_ >= 1.0f) modPhase_ -= 1.0f;
      const float m = (float)kModSamps * std::sin(2.0f * (float)M_PI * modPhase_);

      // Left half: fed by the right half's previous output (nodeD_).
      float aIn = dd1L_.process(x + decay * nodeD_, samps(kDd1L) + m);
      delA_.write(aIn);
      float nodeA = delA_.read(samps(kDelA));
      float cIn = dd2L_.process(decay * dampL_.process(nodeA), samps(kDd2L));
      delC_.write(cIn);
      nodeC_ = delC_.read(samps(kDelC));

      // Right half: fed by the left half's output this sample (nodeC_).
      float bIn = dd1R_.process(x + decay * nodeC_, samps(kDd1R) - m);
      delB_.write(bIn);
      float nodeB = delB_.read(samps(kDelB));
      float dIn = dd2R_.process(decay * dampR_.process(nodeB), samps(kDd2R));
      delD_.write(dIn);
      nodeD_ = delD_.read(samps(kDelD));

      // Output accumulators: several decorrelated interior taps per side.
      float yl = 0.6f * (delC_.tap(266.f * scale_) + delC_.tap(2974.f * scale_) +
                         delA_.tap(1990.f * scale_) - delB_.tap(1066.f * scale_));
      float yr = 0.6f * (delD_.tap(353.f * scale_) + delD_.tap(3067.f * scale_) +
                         delB_.tap(2111.f * scale_) - delA_.tap(266.f * scale_));

      const float w = widthS_.next(widthT);
      const float mid = 0.5f * (yl + yr);
      const float side = 0.5f * (yl - yr) * w;
      const float wetL = mid + side, wetR = mid - side;

      const float mix = mixS_.next(mixT);
      L[i] = dryL * (1.0f - mix) + wetL * mix;
      R[i] = dryR * (1.0f - mix) + wetR * mix;
    }
  }

private:
  // Fractional delay (in samples) for a Dattorro constant at the running rate.
  float samps(double at29761) const noexcept {
    return (float)(at29761 * scale_);
  }
  // Buffer allocation size for that constant, rounded up with a guard.
  int maxSamps(double at29761) const noexcept {
    return (int)std::ceil(at29761 * scale_) + 4;
  }

  // Dattorro constants (samples @ 29761 Hz).
  static constexpr double kInMs[4] = {142, 107, 379, 277};
  static constexpr float kInG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
  static constexpr double kDd1L = 672, kDd1R = 908;
  static constexpr double kDd2L = 1800, kDd2R = 2656;
  static constexpr double kDelA = 4453, kDelB = 4217, kDelC = 3720, kDelD = 3163;
  static constexpr int kModSamps = 16;  // tank allpass excursion

  double sr_ = 48000.0;
  float scale_ = 1.0f;
  rv::Delay1 pre_;
  rv::Allpass in_[4];
  rv::Allpass dd1L_, dd1R_, dd2L_, dd2R_;
  rv::Delay1 delA_, delB_, delC_, delD_;
  OnePole bw_, dampL_, dampR_;
  float nodeC_ = 0.0f, nodeD_ = 0.0f, modPhase_ = 0.0f;
  Smoother mixS_, widthS_;
  Param *mix_, *decay_, *predelay_, *damping_, *bandwidth_, *diffusion_, *width_;
};

}  // namespace fx

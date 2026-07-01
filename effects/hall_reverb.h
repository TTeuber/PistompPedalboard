// effects/hall_reverb.h -- lush modulated hall (Valhalla-style).
//
// A feedback delay network (rv::FDN8) fed through a chain of input-diffusion
// allpasses and a stereo pre-delay. The eight tank lines are slowly modulated so
// the tail shimmers and choruses instead of ringing metallically at the big
// ambient sizes -- the sound Freeverb could never do. Pre-delay, a low-cut into
// the tank, per-band damping and decay time are all real, dialable values.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "reverb_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class Hall : public Effect {
public:
  Hall() : Effect("hall", "Hall") {
    mix_      = addParam("mix",      "Mix",      "%",  0,   100,  30);
    size_     = addParam("size",     "Size",     "%",  0,   100,  60);
    decay_    = addParam("decay",    "Decay",    "s",  0.2f, 15.0f, 3.0f);
    predelay_ = addParam("predelay", "Pre-Delay","ms", 0,   250,  20);
    damping_  = addParam("damping",  "Damping",  "%",  0,   100,  45);
    mod_      = addParam("mod",      "Mod",      "%",  0,   100,  35);
    rate_     = addParam("rate",     "Rate",     "Hz", 0.05f, 2.0f, 0.4f);
    lowcut_   = addParam("lowcut",   "Low Cut",  "Hz", 20,  500,  90);
    width_    = addParam("width",    "Width",    "%",  0,   100,  100);
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int) override {
    sr_ = sr;
    fdn_.prepare(sr, kMaxSize);

    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    predelay_line_.prepare(mono, (int)std::ceil(sr * 0.30));  // 250 ms + guard
    for (int i = 0; i < kNAp; i++) {
      diff_[i].prepare(mono, (int)std::ceil(sr * (kApMs[i] / 1000.0)));
      diff_[i].g = 0.7f;
      apSamps_[i] = (float)(kApMs[i] * 0.001 * sr);
    }
    lowcut_hp_.reset();
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
    widthS_.prepare(sr, 20.0);
    widthS_.snap(width_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float sizeScale = 0.25f + (size_->get() / 100.0f) * (kMaxSize - 0.25f);
    const float rt60 = decay_->get();
    const double dampFc = 18000.0 * std::pow(10.0, -1.3 * (damping_->get() / 100.0));
    const float modMs = (mod_->get() / 100.0f) * 4.0f;
    fdn_.setParams(sizeScale, rt60, dampFc, modMs, rate_->get());

    const float preSamps = predelay_->get() * 0.001f * (float)sr_;
    lowcut_hp_.setCutoff(std::min((double)lowcut_->get(), sr_ * 0.49), sr_);

    const float mixT = mix_->get() / 100.0f;
    const float widthT = width_->get() / 100.0f;

    for (int i = 0; i < n; i++) {
      const float dryL = L[i], dryR = R[i];
      float in = (dryL + dryR) * 0.5f;

      // Pre-delay, then smear through the input-diffusion allpass chain and
      // low-cut before it hits the tank.
      predelay_line_.write(in);
      float d = preSamps > 1.0f ? predelay_line_.read(preSamps) : in;
      for (int a = 0; a < kNAp; a++) d = diff_[a].process(d, apSamps_[a]);
      d = lowcut_hp_.process(d);

      float wL, wR;
      fdn_.process(d, wL, wR);

      // Width: collapse toward mono as width -> 0.
      const float w = widthS_.next(widthT);
      const float mid = 0.5f * (wL + wR);
      const float side = 0.5f * (wL - wR) * w;
      const float wetL = mid + side;
      const float wetR = mid - side;

      const float mix = mixS_.next(mixT);
      L[i] = dryL * (1.0f - mix) + wetL * mix;
      R[i] = dryR * (1.0f - mix) + wetR * mix;
    }
  }

private:
  static constexpr float kMaxSize = 2.0f;
  static constexpr int kNAp = 4;
  static constexpr double kApMs[kNAp] = {13.6, 9.3, 7.6, 5.5};

  double sr_ = 48000.0;
  rv::FDN8 fdn_;
  rv::Delay1 predelay_line_;
  rv::Allpass diff_[kNAp];
  rv::HighCut lowcut_hp_;
  float apSamps_[kNAp] = {0};
  Smoother mixS_, widthS_;
  Param *mix_, *size_, *decay_, *predelay_, *damping_, *mod_, *rate_, *lowcut_,
      *width_;
};

}  // namespace fx

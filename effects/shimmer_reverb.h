// effects/shimmer_reverb.h -- ever-rising ethereal wash (the ambient signature).
//
// The Hall engine (rv::FDN8 + input diffusion + pre-delay) with a pitch shifter
// spliced into the feedback path: the tank's wash is transposed an octave up
// (and/or down), damped, and fed back in. Each pass up the octave stacks on the
// last, so held notes bloom into a shimmering pad that climbs forever -- the
// classic worship/ambient sound no stock reverb gives you.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "reverb_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace fx {

class Shimmer : public Effect {
public:
  Shimmer() : Effect("shimmer", "Shimmer") {
    mix_      = addParam("mix",      "Mix",      "%", 0,   100,  40);
    size_     = addParam("size",     "Size",     "%", 0,   100,  70);
    decay_    = addParam("decay",    "Decay",    "s", 0.2f,15.0f, 4.0f);
    shimmer_  = addParam("shimmer",  "Shimmer",  "%", 0,   100,  50);
    pitch_    = addParam("pitch",    "Pitch",    "",  0,   2,    0);  // enum
    damping_  = addParam("damping",  "Damping",  "%", 0,   100,  40);
    predelay_ = addParam("predelay", "Pre-Delay","ms",0,   250,  20);
    width_    = addParam("width",    "Width",    "%", 0,   100,  100);
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int) override {
    sr_ = sr;
    fdn_.prepare(sr, kMaxSize);

    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    predelay_line_.prepare(mono, (int)std::ceil(sr * 0.30));
    for (int i = 0; i < kNAp; i++) {
      diff_[i].prepare(mono, (int)std::ceil(sr * (kApMs[i] / 1000.0)));
      diff_[i].g = 0.7f;
      apSamps_[i] = (float)(kApMs[i] * 0.001 * sr);
    }
    psUp_.prepare(mono, 60.0f, sr);   psUp_.setRatio(2.0f);    // +12
    psDn_.prepare(mono, 60.0f, sr);   psDn_.setRatio(0.5f);    // -12
    lowcut_.reset();
    lowcut_.setCutoff(90.0, sr);
    shDamp_.reset();
    shFeed_ = 0.0f;

    mixS_.prepare(sr, 20.0);     mixS_.snap(mix_->get() / 100.0f);
    widthS_.prepare(sr, 20.0);   widthS_.snap(width_->get() / 100.0f);
    shAmtS_.prepare(sr, 30.0);   shAmtS_.snap(shimmer_->get() / 100.0f * 0.9f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float sizeScale = 0.25f + (size_->get() / 100.0f) * (kMaxSize - 0.25f);
    const double dampFc = 18000.0 * std::pow(10.0, -1.3 * (damping_->get() / 100.0));
    // Lush fixed modulation -- shimmer wants movement.
    fdn_.setParams(sizeScale, decay_->get(), dampFc, 2.5f, 0.35f);
    // Keep the transposed feedback from getting fizzy.
    shDamp_.setCutoff(std::min(0.6 * dampFc + 2000.0, sr_ * 0.49), sr_);

    const int pitch = (int)(pitch_->get() + 0.5f);  // 0 up, 1 down, 2 dual
    const float preSamps = predelay_->get() * 0.001f * (float)sr_;
    const float mixT = mix_->get() / 100.0f;
    const float widthT = width_->get() / 100.0f;
    const float shAmtT = shimmer_->get() / 100.0f * 0.9f;

    for (int i = 0; i < n; i++) {
      const float dryL = L[i], dryR = R[i];
      float in = (dryL + dryR) * 0.5f + shFeed_;

      predelay_line_.write(in);
      float d = preSamps > 1.0f ? predelay_line_.read(preSamps) : in;
      for (int a = 0; a < kNAp; a++) d = diff_[a].process(d, apSamps_[a]);
      d = lowcut_.process(d);

      float wL, wR;
      fdn_.process(d, wL, wR);
      const float wash = 0.5f * (wL + wR);

      // Transpose the wash and stash it for next sample's injection.
      float sh;
      if (pitch == 0)      sh = psUp_.process(wash);
      else if (pitch == 1) sh = psDn_.process(wash);
      else                 sh = 0.5f * (psUp_.process(wash) + psDn_.process(wash));
      sh = shDamp_.process(sh);
      shFeed_ = sh * shAmtS_.next(shAmtT);

      const float w = widthS_.next(widthT);
      const float mid = 0.5f * (wL + wR);
      const float side = 0.5f * (wL - wR) * w;
      const float wetL = mid + side, wetR = mid - side;

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
  rv::PitchShift psUp_, psDn_;
  rv::HighCut lowcut_;
  OnePole shDamp_;
  float apSamps_[kNAp] = {0};
  float shFeed_ = 0.0f;
  Smoother mixS_, widthS_, shAmtS_;
  Param *mix_, *size_, *decay_, *shimmer_, *pitch_, *damping_, *predelay_,
      *width_;
};

}  // namespace fx

// effects/multitap_delay.h -- Multi-Tap: one delay loop, several rhythmic
// output taps. Time (or the synced division) sets the LOOP length; each
// pattern places up to four taps at fixed fractions of it with their own
// gains and stereo pans, so the echoes come back as a rhythm -- dotted-8th
// lead lines, triplets, a shuffle, or the golden-ratio spread for
// non-rhythmic ambient washes. Feedback circulates the full loop (the 1.0
// tap), darkened by Tone and thinned at 100 Hz like the other delays.
//
// The loop is MONO (input is the L+R average, like the reverbs); stereo comes
// from the constant-power tap pans, scaled toward center by Width. Pattern
// changes never click: tap gains/pans move through 20 ms smoothers and tap
// positions slide there through their own smoothers (a brief, quiet chirp --
// the same tape-bend aesthetic as the Time glide). Freeze locks the loop at
// unity with the input muted; the pattern taps keep replaying the frozen bar.

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

class MultiTap : public Effect {
public:
  MultiTap() : Effect("multitap", "Multi-Tap") {
    time_    = addParam("time",     "Time",     "ms", 50, 1500, 500);
    pattern_ = addParam("pattern",  "Pattern",  "",   0,  kNumPatterns - 1, 1);  // enum
    fb_      = addParam("feedback", "Feedback", "%",  0,  90,   35);
    mix_     = addParam("mix",      "Mix",      "%",  0,  100,  40);
    tone_    = addParam("tone",     "Tone",     "%",  0,  100,  55);
    width_   = addParam("width",    "Width",    "%",  0,  100,  100);
    // Default Div = 1/2: with the default Dotted 8th pattern the synced taps
    // land on the dotted-eighth grid.
    sync_    = addParam("sync",     "Sync",     "",   0, 1, 0);              // toggle
    div_     = addParam("div",      "Div",      "",   0, tempo::kDivCount - 1, 1);  // enum
    freeze_  = addParam("freeze",   "Freeze",   "",   0, 1, 0);              // toggle
  }

  bool hasTails() const noexcept override { return true; }

  void prepare(double sr, int) override {
    sr_ = sr;
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;  // we push/pop one sample at a time
    mono.numChannels = 1;
    line_.prepare(mono, (int)std::ceil(sr * (kMaxMs / 1000.0)));
    tone1_.reset();
    hp_.setCutoff(100.0, sr);
    hp_.reset();
    fbS_.prepare(sr, 20.0);
    mixS_.prepare(sr, 20.0);
    fbS_.snap(fb_->get() / 100.0f);
    mixS_.snap(mix_->get() / 100.0f);
    frz_.prepare(sr);
    glide_.prepare(sr);
    glide_.snap(targetSamps());
    const Pattern& p = kPatterns[patternIdx()];
    for (int k = 0; k < kMaxTaps; k++) {
      fracS_[k].prepare(sr, 50.0);
      gLS_[k].prepare(sr, 20.0);
      gRS_[k].prepare(sr, 20.0);
      fracS_[k].snap(p.taps[k].frac);
      float gl, gr;
      panGains(p.taps[k], width_->get() / 100.0f, gl, gr);
      gLS_[k].snap(gl);
      gRS_[k].snap(gr);
    }
  }

  void process(float* L, float* R, int n) noexcept override {
    const float dTarget = targetSamps();
    const float fbT = fb_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const bool frozen = freeze_->get() > 0.5f;
    const float wid = width_->get() / 100.0f;
    const Pattern& p = kPatterns[patternIdx()];
    // Tone: darken the repeats from ~1.2 kHz up to ~12 kHz.
    const double fc = 1200.0 * std::pow(10.0, tone_->get() / 100.0);
    tone1_.setCutoff(std::min(fc, sr_ * 0.45), sr_);
    // Per-tap targets for this block. Unused slots fade to silence but keep
    // their last position, so nothing slides around inaudibly.
    float fracT[kMaxTaps], gLT[kMaxTaps], gRT[kMaxTaps];
    for (int k = 0; k < kMaxTaps; k++) {
      const bool used = p.taps[k].gain > 0.0f;
      fracT[k] = used ? p.taps[k].frac : fracS_[k].y;
      panGains(p.taps[k], wid, gLT[k], gRT[k]);
    }

    for (int i = 0; i < n; i++) {
      const float fb = fbS_.next(fbT);
      const float mix = mixS_.next(mixT);
      const float frz = frz_.next(frozen);
      const float d = glide_.next(dTarget);

      const float inL = L[i], inR = R[i];
      const float in = 0.5f * (inL + inR);

      // Pattern taps (non-advancing reads), panned constant-power.
      float wetL = 0.0f, wetR = 0.0f;
      for (int k = 0; k < kMaxTaps; k++) {
        const float pos = std::max(1.0f, fracS_[k].next(fracT[k]) * d);
        const float tap = line_.tap(pos);
        wetL += tap * gLS_[k].next(gLT[k]);
        wetR += tap * gRS_[k].next(gRT[k]);
      }

      // Feedback: the full loop (the advancing read at 1.0 x Time), darkened
      // and thinned; coloring crossfades out as freeze engages.
      const float loop = line_.read(std::max(1.0f, d));
      const float colored = hp_.process(tone1_.process(loop));
      const float loopGain = fb * (1.0f - frz) + frz;
      const float x = in * (1.0f - frz) +
                      (colored * (1.0f - frz) + loop * frz) * loopGain;
      line_.write(dly::softLimit(x));

      L[i] = inL * (1.0f - mix) + wetL * mix;
      R[i] = inR * (1.0f - mix) + wetR * mix;
    }
  }

private:
  static constexpr int kMaxTaps = 4;
  static constexpr int kNumPatterns = 6;

  struct Tap {
    float frac, gain, pan;  // fraction of Time, level, pan -1..+1
  };
  struct Pattern {
    Tap taps[kMaxTaps];  // gain 0 = slot unused
  };

  // The rhythm menu. Golden (phi-spaced) is the non-rhythmic ambient wash.
  // Order is the wire contract with the web UI's `pattern` dropdown -- append,
  // never reorder.
  static constexpr Pattern kPatterns[kNumPatterns] = {
      {{{0.25f, 0.90f, -0.6f}, {0.50f, 0.75f, +0.6f}, {0.75f, 0.60f, -0.3f}, {1.00f, 0.50f, +0.3f}}},  // Quarters
      {{{0.375f, 0.85f, -0.5f}, {0.75f, 0.70f, +0.5f}, {1.00f, 0.55f, 0.0f}, {0.0f, 0.0f, 0.0f}}},     // Dotted 8th
      {{{0.333f, 0.85f, -0.5f}, {0.667f, 0.70f, +0.5f}, {1.00f, 0.55f, 0.0f}, {0.0f, 0.0f, 0.0f}}},    // Triplet
      {{{0.375f, 0.70f, -0.4f}, {0.50f, 0.85f, +0.4f}, {0.875f, 0.60f, -0.4f}, {1.00f, 0.75f, +0.4f}}}, // Shuffle
      {{{0.382f, 0.80f, -0.7f}, {0.618f, 0.65f, +0.7f}, {1.00f, 0.50f, 0.0f}, {0.0f, 0.0f, 0.0f}}},    // Golden
      {{{0.50f, 0.85f, -1.0f}, {1.00f, 0.85f, +1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}},        // Ping 8th
  };

  int patternIdx() const noexcept {
    return std::clamp((int)pattern_->get(), 0, kNumPatterns - 1);
  }

  // Constant-power pan; Width pulls the pans toward center.
  static void panGains(const Tap& t, float width, float& gl, float& gr) noexcept {
    const float a = (t.pan * width + 1.0f) * (float)M_PI * 0.25f;
    gl = t.gain * std::cos(a);
    gr = t.gain * std::sin(a);
  }

  float targetSamps() const noexcept {
    const float ms = dly::targetMs(sync_->get() > 0.5f, (int)div_->get(),
                                   time_->get());
    return std::clamp(ms, 1.0f, kMaxMs) * 0.001f * (float)sr_;
  }

  static constexpr float kMaxMs = 1500.0f;
  double sr_ = 48000.0;
  rv::Delay1 line_;
  OnePole tone1_;
  dly::HighPass1 hp_;
  Smoother fbS_, mixS_;
  Smoother fracS_[kMaxTaps], gLS_[kMaxTaps], gRS_[kMaxTaps];
  dly::Freeze frz_;
  dly::GlideValue glide_;
  Param *time_, *pattern_, *fb_, *mix_, *tone_, *width_, *sync_, *div_,
      *freeze_;
};

}  // namespace fx

// effects/chorus.h -- multi-voice chorus, post-amp (replaces the JUCE wrapper).
//
// Up to three modulated delay taps per side, staggered in base delay and LFO
// phase/rate so they never lock -- the lush "rack" chorus a single-voice
// modulated delay can't make. The wet path is darkened by a one-pole (~8 kHz),
// the BBD-era voice that flatters worship cleans. Voices switches 1/2/3 taps
// through smoothed per-voice gains (click-free, no reset); Width mid/side-
// scales the wet image from mono up to the full quarter-cycle L/R spread.
//
// The old wrapper's Feedback knob is gone: a multi-voice bank has no natural
// single feedback point, and at its 10% default it was barely audible. Old
// presets restore everything else (rate/depth/mix ids unchanged).

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "mod_dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace fx {

class Chorus : public Effect {
public:
  Chorus() : Effect("chorus", "Chorus") {
    rate_   = addParam("rate",   "Rate",   "Hz", 0.05f, 8.0f, 0.8f);
    depth_  = addParam("depth",  "Depth",  "%",  0, 100, 30);
    voices_ = addParam("voices", "Voices", "",   0, 2,   1);  // enum: 1/2/3
    width_  = addParam("width",  "Width",  "%",  0, 100, 100);
    mix_    = addParam("mix",    "Mix",    "%",  0, 100, 35);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    juce::dsp::ProcessSpec mono;
    mono.sampleRate = sr;
    mono.maximumBlockSize = 1;
    mono.numChannels = 1;
    const int maxSamps =
        (int)std::ceil(sr * ((kBaseMs[kVoices - 1] + kSweepMs) / 1000.0));
    for (int ch = 0; ch < 2; ch++) {
      line_[ch].prepare(mono, maxSamps);
      dark_[ch].setCutoff(8000.0, sr);
      dark_[ch].reset();
      for (int v = 0; v < kVoices; v++) {
        baseSamps_[v] = (float)(kBaseMs[v] * 0.001 * sr);
        // Phase-spread voices; R a quarter cycle off L for the stereo bloom.
        lfo_[ch][v].setPhase(0.33f * (float)v + (ch ? 0.25f : 0.0f));
      }
    }
    sweepS_.prepare(sr, 20.0);
    sweepS_.snap(sweepTarget());
    widthS_.prepare(sr, 20.0);
    widthS_.snap(width_->get() / 100.0f);
    mixS_.prepare(sr, 20.0);
    mixS_.snap(mix_->get() / 100.0f);
    const int nv = (int)(voices_->get() + 0.5f);
    for (int v = 0; v < kVoices; v++) {
      gS_[v].prepare(sr, 30.0);
      gS_[v].snap(v <= nv ? 1.0f : 0.0f);
    }
  }

  void process(float* L, float* R, int n) noexcept override {
    const float sweepT = sweepTarget();
    const float widthT = width_->get() / 100.0f;
    const float mixT = mix_->get() / 100.0f;
    const int nv = (int)(voices_->get() + 0.5f);
    const float rate = rate_->get();
    for (int ch = 0; ch < 2; ch++)
      for (int v = 0; v < kVoices; v++)
        lfo_[ch][v].setRate(rate * kRateSpread[v], sr_);

    float* io[2] = {L, R};
    for (int i = 0; i < n; i++) {
      // Sweep/gains/width/mix smoothed per sample: they all scale a read tap
      // or a wet gain, and block-rate steps there are audible clicks.
      const float amp = sweepS_.next(sweepT);
      float g[kVoices], sumsq = 0.0f;
      for (int v = 0; v < kVoices; v++) {
        g[v] = gS_[v].next(v <= nv ? 1.0f : 0.0f);
        sumsq += g[v] * g[v];
      }
      // Equal-power makeup so 1 vs 3 voices sits at the same loudness.
      const float norm = 1.0f / std::sqrt(std::max(sumsq, 0.5f));
      const float width = widthS_.next(widthT);
      const float mix = mixS_.next(mixT);

      float wet[2];
      for (int ch = 0; ch < 2; ch++) {
        const float in = io[ch][i];
        line_[ch].write(in);
        float sum = 0.0f;
        for (int v = 0; v < kVoices; v++) {
          const float d =
              baseSamps_[v] + amp * Lfo::sine(lfo_[ch][v].next());
          const float tapv =
              (v < kVoices - 1) ? line_[ch].tap(d) : line_[ch].read(d);
          sum += tapv * g[v];
        }
        wet[ch] = dark_[ch].process(sum * norm);
      }

      // Width: mid/side the wet pair (dry stays put).
      const float mid = 0.5f * (wet[0] + wet[1]);
      const float side = 0.5f * (wet[0] - wet[1]) * width;
      const float wetL = mid + side, wetR = mid - side;

      L[i] = L[i] * (1.0f - mix) + wetL * mix;
      R[i] = R[i] * (1.0f - mix) + wetR * mix;
    }
  }

private:
  // Sweep amplitude (samples) at the current Depth. The deepest sweep (2 ms)
  // stays under the shortest base (5.5 ms), so taps never go negative.
  float sweepTarget() const noexcept {
    return depth_->get() / 100.0f * (float)(kSweepMs * 0.001) * (float)sr_;
  }

  static constexpr int kVoices = 3;
  static constexpr double kBaseMs[kVoices] = {5.5, 8.5, 12.0};
  // Slight per-voice detune of the LFO rate keeps the voices from breathing
  // in unison -- the "never locks" shimmer of a rack chorus.
  static constexpr float kRateSpread[kVoices] = {1.0f, 1.13f, 0.87f};
  static constexpr double kSweepMs = 2.0;

  double sr_ = 48000.0;
  md::ModDelay line_[2];
  OnePole dark_[2];
  Lfo lfo_[2][kVoices];
  float baseSamps_[kVoices] = {0};
  Smoother sweepS_, widthS_, mixS_, gS_[kVoices];
  Param *rate_, *depth_, *voices_, *width_, *mix_;
};

}  // namespace fx

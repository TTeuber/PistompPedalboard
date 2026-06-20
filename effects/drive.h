// effects/drive.h -- the hand-written drive/distortion pedal.
//
// The one effect we build by hand (everything else leans on juce::dsp). Three
// voicings a worship student actually reaches for, a tone rolloff, and an output
// level. Waveshaping is run through a 2x oversampler (dsp_util.h) so the added
// harmonics don't alias into nasty inharmonic fizz.
//
// Params are all 0..100% / index so they map cleanly onto web sliders; the
// musical ranges (gain, tone Hz) are derived internally.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <cmath>

namespace fx {

enum DriveType { kOverdrive = 0, kDistortion = 1, kFuzz = 2 };

class Drive : public Effect {
public:
  Drive() : Effect("drive", "Drive") {
    type_  = addParam("type",  "Type",  "",  0, 2,   kOverdrive);  // 0/1/2
    drive_ = addParam("drive", "Drive", "%", 0, 100, 40);
    tone_  = addParam("tone",  "Tone",  "%", 0, 100, 60);
    level_ = addParam("level", "Level", "%", 0, 100, 70);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr);
    osR_.prepare(sr);
    toneL_.reset();
    toneR_.reset();
  }

  void process(float* L, float* R, int n) noexcept override {
    const int type = (int)std::lround(type_->get());
    const float gain = 1.0f + drive_->get() / 100.0f * 30.0f;  // 1x .. 31x
    const float level = level_->get() / 100.0f;
    // Tone: log-ish sweep ~700 Hz (dark) .. ~9 kHz (open).
    const double fc = 700.0 * std::pow(9000.0 / 700.0, tone_->get() / 100.0);
    toneL_.setCutoff(fc, sr_);
    toneR_.setCutoff(fc, sr_);

    auto shaper = [type, gain](float x) noexcept -> float {
      float v = x * gain;
      switch (type) {
        case kDistortion:  // harder, slightly asymmetric clip
          return std::tanh(v + 0.15f * v * v);
        case kFuzz:        // aggressive near-square clip
          return std::tanh(3.0f * v);
        case kOverdrive:   // smooth soft clip
        default:
          return std::tanh(v);
      }
    };

    for (int i = 0; i < n; i++) {
      float yl = osL_.process(L[i], shaper);
      float yr = osR_.process(R[i], shaper);
      L[i] = toneL_.process(yl) * level;
      R[i] = toneR_.process(yr) * level;
    }
  }

private:
  double sr_ = 48000.0;
  Oversampler2x osL_, osR_;
  OnePole toneL_, toneR_;
  Param *type_, *drive_, *tone_, *level_;
};

}  // namespace fx

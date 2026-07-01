// effects/drive_base.h -- shared guts for the overdrive / distortion / fuzz pedals.
//
// All three are the same hand-built circuit: a 2x-oversampled waveshaper
// (dsp_util.h) so the added harmonics don't alias, a post-shaper tone rolloff,
// and an output level. They differ ONLY in the nonlinearity. CRTP keeps that one
// difference a `static shape()` the compiler inlines into the per-sample loop --
// no virtual call on the audio thread, no copy-pasted process().
//
// Params are 0..100% so they map cleanly onto web sliders; the musical ranges
// (gain, tone Hz) are derived internally, same as the old combined Drive pedal.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <cmath>
#include <string>
#include <utility>

namespace fx {

template <class Derived>
class DriveBase : public Effect {
public:
  DriveBase(std::string type, std::string name, float defaultDrive)
      : Effect(std::move(type), std::move(name)) {
    drive_ = addParam("drive", "Drive", "%", 0, 100, defaultDrive);
    tone_  = addParam("tone",  "Tone",  "%", 0, 100, 60);
    level_ = addParam("level", "Level", "%", 0, 100, 70);
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    osL_.prepare(sr);
    osR_.prepare(sr);
    toneL_.reset();
    toneR_.reset();
    gainS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    gainS_.snap(driveGain());
    levelS_.snap(level_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    const float gainT = driveGain();
    const float levelT = level_->get() / 100.0f;
    // Tone: log-ish sweep ~700 Hz (dark) .. ~9 kHz (open).
    const double fc = 700.0 * std::pow(9000.0 / 700.0, tone_->get() / 100.0);
    toneL_.setCutoff(fc, sr_);
    toneR_.setCutoff(fc, sr_);

    for (int i = 0; i < n; i++) {
      // Smoothed per sample: a swept Drive/Level knob glides instead of
      // stepping per block (a gain step into a waveshaper is a loud click).
      const float g = gainS_.next(gainT);
      const float level = levelS_.next(levelT);
      auto shaper = [g](float x) noexcept -> float {
        return Derived::shape(x * g);  // inlined; the only per-pedal difference
      };
      float yl = osL_.process(L[i], shaper);
      float yr = osR_.process(R[i], shaper);
      L[i] = toneL_.process(yl) * level;
      R[i] = toneR_.process(yr) * level;
    }
  }

private:
  float driveGain() const noexcept {
    return 1.0f + drive_->get() / 100.0f * 30.0f;  // 1x .. 31x
  }

  double sr_ = 48000.0;
  Oversampler2x osL_, osR_;
  OnePole toneL_, toneR_;
  Smoother gainS_, levelS_;
  Param *drive_, *tone_, *level_;
};

}  // namespace fx

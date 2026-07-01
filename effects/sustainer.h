// effects/sustainer.h -- Big Muff-family fuzz/sustainer.
//
// The Muff sound is CASCADED clipping: two saturation stages in series, each
// with its own filtering, so as a note decays the later stage keeps clipping
// long after the first has gone linear -- that hand-off is the endless
// sustain. Inter-stage one-poles do the housekeeping the real circuit's
// coupling caps do: 80 Hz highpasses stop low-end fart-out and stage-to-stage
// DC creep, 8 kHz lowpasses shave fizz between stages. The whole cascade runs
// inside a 4x oversampler.
//
// Tone is the classic Muff tilt (drive_dsp.h ScoopTone): a lowpass arm
// crossfaded against a highpass arm with a gap between the corners, so noon
// leaves the famous mid scoop (~-9 dB around 900 Hz); 0 is woolly, 100 is
// cutting. The Sustain knob is the first stage's gain (param id stays "drive"
// so per-kind presets and rigs keep working). Makeup ~ gpre^-0.27 flattens the
// sweep.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"
#include "drive_dsp.h"

#include <cmath>

namespace fx {

class Sustainer : public Effect {
public:
  Sustainer() : Effect("sustainer", "Sustainer") {
    drive_ = addParam("drive", "Sustain", "%", 0, 100, 60);
    tone_  = addParam("tone",  "Tone",    "%", 0, 100, 50);
    level_ = addParam("level", "Level",   "%", 0, 100, 60);
  }

  void prepare(double sr, int) override {
    osL_.prepare(sr, 4);
    osR_.prepare(sr, 4);
    // The whole cascade lives in the shaper functor => oversampled-rate
    // cutoffs for every inter-stage filter.
    const double osr = sr * 4.0;
    for (Stage* s : {&stL_, &stR_}) {
      s->hpIn.setCutoff(80.0, osr);
      s->hpMid.setCutoff(80.0, osr);
      s->hpOut.setCutoff(80.0, osr);
      s->lpMid.setCutoff(8000.0, osr);
      s->lpOut.setCutoff(8000.0, osr);
      s->reset();
    }
    scoopL_.prepare(sr);
    scoopR_.prepare(sr);
    dcL_.prepare(sr);
    dcR_.prepare(sr);
    gainS_.prepare(sr, 20.0);
    toneS_.prepare(sr, 20.0);
    levelS_.prepare(sr, 20.0);
    gainS_.snap(preGain());
    toneS_.snap(tone_->get() / 100.0f);
    levelS_.snap(levelTarget(preGain()));
  }

  void process(float* L, float* R, int n) noexcept override {
    const float gT = preGain();
    const float toneT = tone_->get() / 100.0f;
    const float levelT = levelTarget(gT);

    for (int i = 0; i < n; i++) {
      const float g = gainS_.next(gT);
      const float t = toneS_.next(toneT);
      const float lvl = levelS_.next(levelT);

      float yl = osL_.process(L[i], [this, g](float u) noexcept {
        return stL_.run(u, g);
      });
      float yr = osR_.process(R[i], [this, g](float u) noexcept {
        return stR_.run(u, g);
      });
      L[i] = scoopL_.process(dcL_.process(yl), t) * lvl;
      R[i] = scoopR_.process(dcR_.process(yr), t) * lvl;
    }
  }

private:
  // Per-channel cascade state: HP80 -> tanh(g*u) -> HP80 -> LP8k -> tanh(3v)
  // -> HP80 -> LP8k, all at the oversampled rate.
  struct Stage {
    drv::HighPass1 hpIn, hpMid, hpOut;
    OnePole lpMid, lpOut;

    void reset() noexcept {
      hpIn.reset();
      hpMid.reset();
      hpOut.reset();
      lpMid.reset();
      lpOut.reset();
    }
    float run(float u, float g) noexcept {
      float v = std::tanh(g * hpIn.process(u));
      v = lpMid.process(hpMid.process(v));
      v = std::tanh(3.0f * v);
      return lpOut.process(hpOut.process(v));
    }
  };

  // Sustain -> first-stage gain, ~+6..+30 dB (stage two is a fixed x3).
  float preGain() const noexcept {
    return 2.0f + 28.0f * drive_->get() / 100.0f;
  }
  // Level knob with loudness compensation folded in. The cascade saturates
  // almost immediately (raw RMS is flat above ~25% sustain), so alpha is a
  // gentle 0.27 -- measured with the render sweep.
  float levelTarget(float g) const noexcept {
    return level_->get() / 100.0f * 2.3f * std::pow(g, -0.27f);
  }

  drv::OversamplerIIR osL_, osR_;
  Stage stL_, stR_;
  drv::ScoopTone scoopL_, scoopR_;
  drv::DcBlock dcL_, dcR_;
  Smoother gainS_, toneS_, levelS_;
  Param *drive_, *tone_, *level_;
};

}  // namespace fx

// effects/amp_nam.h -- the amp block: a Neural Amp Modeler capture as an Effect.
//
// Reuses the exact NAM inference path proven in nam/nam_amp.cpp: a prewarmed
// nam::DSP processed mono. The model is mono-in/mono-out, so this is where the
// chain collapses to mono -- we run the left channel through the amp and fan the
// result to both channels. Downstream effects (chorus/delay/reverb) then open it
// back up to real stereo.
//
// The model itself is owned by main() (loaded off the RT path); this Effect holds
// a borrowed pointer and never frees it.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include "NAM/dsp.h"

#include <vector>

namespace fx {

class AmpNam : public Effect {
public:
  // `model` may be null (e.g. offline tests without a .nam); then the amp is a
  // clean unity block. `displayName` shows on the LCD / web (usually the file).
  AmpNam(nam::DSP* model, std::string displayName)
      : Effect("amp", displayName.empty() ? "Amp" : std::move(displayName)),
        model_(model) {
    drive_ = addParam("drive", "Input",  "%", 0, 400, 100);
    level_ = addParam("level", "Output", "%", 0, 200, 100);
  }

  void prepare(double sr, int maxBlock) override {
    in_.assign((size_t)maxBlock, 0.0);
    out_.assign((size_t)maxBlock, 0.0);
    if (model_) model_->ResetAndPrewarm(sr, maxBlock);
    gS_.prepare(sr, 20.0);
    lvlS_.prepare(sr, 20.0);
    gS_.snap(drive_->get() / 100.0f);
    lvlS_.snap(level_->get() / 100.0f);
  }

  void process(float* L, float* R, int n) noexcept override {
    // Input/Output trims smoothed per sample so swept knobs glide (the input
    // trim especially -- a gain step into the model is a click, amplified).
    const float gT = drive_->get() / 100.0f;
    const float lvlT = level_->get() / 100.0f;

    if (!model_) {  // no model: unity, just apply trims and fan to stereo
      for (int i = 0; i < n; i++) {
        float y = L[i] * gS_.next(gT) * lvlS_.next(lvlT);
        L[i] = y;
        R[i] = y;
      }
      return;
    }

    for (int i = 0; i < n; i++)
      in_[(size_t)i] = (double)(L[i] * gS_.next(gT));
    double* ia[1] = {in_.data()};
    double* oa[1] = {out_.data()};
    model_->process(ia, oa, n);
    for (int i = 0; i < n; i++) {
      float y = (float)out_[(size_t)i] * lvlS_.next(lvlT);
      L[i] = y;
      R[i] = y;
    }
  }

private:
  nam::DSP* model_;            // borrowed, prewarmed; never mutated on RT thread
  std::vector<double> in_, out_;
  Smoother gS_, lvlS_;
  Param *drive_, *level_;
};

}  // namespace fx

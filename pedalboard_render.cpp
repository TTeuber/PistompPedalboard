// pedalboard_render.cpp -- OFFLINE chain benchmark (no hardware, runs on the Mac).
//
// Pushes a synthetic guitar-ish signal through the full effect chain in 64-frame
// blocks and reports the real-time factor (audio-seconds / wall-seconds; must be
// > 1.0 to play live) plus output sanity (peak/RMS). This is the verification
// step that catches a too-heavy default before deploying to the Pi.
//
// Usage: pedalboard_render [model.nam]   (model optional; clean amp if omitted)

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include "chain.h"
#include "pedal_controls.h"
#include "effects/tuner.h"
#include "effects/gate.h"
#include "effects/comp.h"
#include "effects/drive.h"
#include "effects/amp_nam.h"
#include "effects/eq.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/reverb.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
  const double sr = 48000.0;
  const int block = 64;
  const double seconds = 10.0;
  const int total = (int)(sr * seconds);

  std::unique_ptr<nam::DSP> model;
  if (argc > 1) {
    try {
      model = nam::get_dsp(std::filesystem::path(argv[1]));
      printf("Loaded model: %s\n", argv[1]);
    } catch (const std::exception& e) {
      printf("model load failed (%s) -- using clean amp\n", e.what());
    }
  } else {
    printf("No model given -- using clean amp block.\n");
  }

  Chain chain;
  // Singletons (prefix/suffix) by section; FX go in grid slots like the live app.
  chain.add(std::make_unique<fx::Tuner>())->section = Section::Hidden;  // off by default
  chain.add(std::make_unique<fx::Gate>())->section = Section::Input;
  chain.add(std::make_unique<fx::Comp>())->section = Section::Input;
  chain.add(std::make_unique<fx::AmpNam>(model.get(), "render"))->section = Section::Output;
  chain.add(std::make_unique<fx::EQ>())->section = Section::Output;
  chain.fxPlaceInitial(0, std::make_unique<fx::Drive>());
  chain.fxPlaceInitial(1, std::make_unique<fx::Chorus>());
  chain.fxPlaceInitial(2, std::make_unique<fx::Delay>());
  chain.fxPlaceInitial(3, std::make_unique<fx::Reverb>());
  chain.finalize();  // partition prefix/suffix + publish FX order
  chain.prepare(sr, block);

  // Synthetic input: decaying plucked 110 Hz + a little noise, mono fanned L=R.
  std::vector<float> L(block), R(block);
  double phase = 0.0;
  const double dphi = 2.0 * M_PI * 110.0 / sr;
  unsigned rng = 22222;

  double peak = 0.0, sumsq = 0.0;
  long processed = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (int n = 0; n < total; n += block) {
    int frames = std::min(block, total - n);
    for (int i = 0; i < frames; i++) {
      double env = std::exp(-((n + i) % (int)sr) / (sr * 0.4));  // re-pluck each sec
      rng = rng * 1664525u + 1013904223u;
      float noise = ((float)(int)(rng >> 9) / 4194304.0f - 1.0f) * 0.02f;
      float x = (float)(std::sin(phase) * 0.5 * env) + noise;
      phase += dphi;
      L[(size_t)i] = x; R[(size_t)i] = x;
    }
    chain.process(L.data(), R.data(), frames);
    for (int i = 0; i < frames; i++) {
      double a = std::fabs(L[(size_t)i]);
      if (a > peak) peak = a;
      sumsq += (double)L[(size_t)i] * L[(size_t)i];
    }
    processed += frames;
  }
  auto t1 = std::chrono::steady_clock::now();

  double wall = std::chrono::duration<double>(t1 - t0).count();
  double rtf = seconds / wall;
  double rms = std::sqrt(sumsq / (double)processed);
  printf("Processed %.1f s of audio in %.3f s wall  ->  RTF %.2fx %s\n",
         seconds, wall, rtf, rtf > 1.0 ? "(realtime OK)" : "(TOO SLOW)");
  printf("Output: peak %.3f, rms %.3f\n", peak, rms);
  return (rtf > 1.0 && peak > 0.0 && std::isfinite(peak)) ? 0 : 1;
}

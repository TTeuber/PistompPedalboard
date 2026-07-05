// pedalboard_render.cpp -- OFFLINE chain benchmark (no hardware, runs on the Mac).
//
// Default mode pushes a synthetic guitar-ish signal through the full effect
// chain in 64-frame blocks and reports the real-time factor (audio-seconds /
// wall-seconds; must be > 1.0 to play live) plus output sanity (peak/RMS/DC).
// This is the verification step that catches a too-heavy default before
// deploying to the Pi.
//
// Isolated-pedal verification mode (--fx, or its older alias --drive) puts one
// pedal in an otherwise empty chain so its numbers aren't laundered through
// the amp/EQ. Kinds: the five drives (overdrive|golddrive|distortion|fuzz|
// sustainer) and the modulation collection (chorus|phaser|flanger|vibe|
// dimension|detune|rotary).
//   --sweep             step Drive 0/25/50/75/100, print RMS + DC per step
//                       (drives only: checks the loudness-compensation
//                       flatness and that the DC blockers hold at every gain)
//   --freq <hz>         steady sine input instead of the pluck (aliasing
//                       checks: render a high note, FFT the dump)
//   -o <file>           dump the left channel as raw float32 for inspection
//
// Usage: pedalboard_render [model.nam] [--fx kind [--sweep]] [--freq hz] [-o out.raw]

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include "chain.h"
#include "pedal_controls.h"
#include "effects/tuner.h"
#include "effects/gate.h"
#include "effects/comp.h"
#include "effects/overdrive.h"
#include "effects/gold_drive.h"
#include "effects/distortion.h"
#include "effects/fuzz.h"
#include "effects/sustainer.h"
#include "effects/amp_nam.h"
#include "effects/eq.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/detune.h"
#include "effects/dimension.h"
#include "effects/flanger.h"
#include "effects/hall_reverb.h"
#include "effects/phaser.h"
#include "effects/rotary.h"
#include "effects/uni_vibe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<Effect> makePedal(const std::string& kind) {
  if (kind == "overdrive")  return std::make_unique<fx::Overdrive>();
  if (kind == "golddrive")  return std::make_unique<fx::GoldDrive>();
  if (kind == "distortion") return std::make_unique<fx::Distortion>();
  if (kind == "fuzz")       return std::make_unique<fx::Fuzz>();
  if (kind == "sustainer")  return std::make_unique<fx::Sustainer>();
  if (kind == "chorus")     return std::make_unique<fx::Chorus>();
  if (kind == "phaser")     return std::make_unique<fx::Phaser>();
  if (kind == "flanger")    return std::make_unique<fx::Flanger>();
  if (kind == "vibe")       return std::make_unique<fx::UniVibe>();
  if (kind == "dimension")  return std::make_unique<fx::Dimension>();
  if (kind == "detune")     return std::make_unique<fx::Detune>();
  if (kind == "rotary")     return std::make_unique<fx::Rotary>();
  return nullptr;
}

// The shared test signal: a decaying "pluck" re-struck every second (default),
// or a steady sine at freqHz when freqHz > 0.
struct TestSignal {
  double sr, phase = 0.0, dphi;
  double freqHz;  // <= 0 means pluck mode
  unsigned rng = 22222;

  TestSignal(double sr_, double freqHz_)
      : sr(sr_), dphi(2.0 * M_PI * (freqHz_ > 0 ? freqHz_ : 110.0) / sr_),
        freqHz(freqHz_) {}

  float next(long n) noexcept {
    float x;
    if (freqHz > 0) {
      x = (float)(std::sin(phase) * 0.5);
    } else {
      double env = std::exp(-(double)(n % (long)sr) / (sr * 0.4));  // re-pluck each sec
      rng = rng * 1664525u + 1013904223u;
      float noise = ((float)(int)(rng >> 9) / 4194304.0f - 1.0f) * 0.02f;
      x = (float)(std::sin(phase) * 0.5 * env) + noise;
    }
    phase += dphi;
    return x;
  }
};

struct Stats {
  double peak = 0.0, sumsq = 0.0, sum = 0.0;
  long n = 0;

  void accum(const float* L, int frames) noexcept {
    for (int i = 0; i < frames; i++) {
      double a = std::fabs(L[i]);
      if (a > peak) peak = a;
      sumsq += (double)L[i] * L[i];
      sum += L[i];
      n++;
    }
  }
  double rms() const { return n ? std::sqrt(sumsq / (double)n) : 0.0; }
  double mean() const { return n ? sum / (double)n : 0.0; }
  double rmsDb() const { return 20.0 * std::log10(rms() + 1e-12); }
};

}  // namespace

int main(int argc, char** argv) {
  const double sr = 48000.0;
  const int block = 64;

  std::string modelPath, driveKind, outPath;
  double freqHz = 0.0;
  bool sweep = false;
  for (int a = 1; a < argc; a++) {
    if ((!std::strcmp(argv[a], "--fx") || !std::strcmp(argv[a], "--drive")) &&
        a + 1 < argc) driveKind = argv[++a];
    else if (!std::strcmp(argv[a], "--sweep")) sweep = true;
    else if (!std::strcmp(argv[a], "--freq") && a + 1 < argc) freqHz = std::atof(argv[++a]);
    else if (!std::strcmp(argv[a], "-o") && a + 1 < argc) outPath = argv[++a];
    else modelPath = argv[a];
  }

  Chain chain;
  Effect* drive = nullptr;
  std::unique_ptr<nam::DSP> model;

  if (!driveKind.empty()) {
    // Isolated pedal chain: just the pedal, nothing to launder its output.
    auto fx = makePedal(driveKind);
    if (!fx) {
      printf("unknown kind '%s' (overdrive|golddrive|distortion|fuzz|sustainer|"
             "chorus|phaser|flanger|vibe|dimension|detune|rotary)\n",
             driveKind.c_str());
      return 1;
    }
    drive = chain.fxPlaceInitial(0, std::move(fx));
    printf("Isolated pedal: %s\n", drive->name().c_str());
  } else {
    if (!modelPath.empty()) {
      try {
        model = nam::get_dsp(std::filesystem::path(modelPath));
        printf("Loaded model: %s\n", modelPath.c_str());
      } catch (const std::exception& e) {
        printf("model load failed (%s) -- using clean amp\n", e.what());
      }
    } else {
      printf("No model given -- using clean amp block.\n");
    }
    // Singletons (prefix/suffix) by section; FX in grid slots like the live app.
    chain.add(std::make_unique<fx::Tuner>())->section = Section::Hidden;  // off by default
    chain.add(std::make_unique<fx::Gate>())->section = Section::Input;
    chain.add(std::make_unique<fx::Comp>())->section = Section::Input;
    chain.add(std::make_unique<fx::AmpNam>(model.get(), "render"))->section = Section::Output;
    chain.add(std::make_unique<fx::EQ>())->section = Section::Output;
    chain.fxPlaceInitial(0, std::make_unique<fx::Overdrive>());
    chain.fxPlaceInitial(1, std::make_unique<fx::Chorus>());
    chain.fxPlaceInitial(2, std::make_unique<fx::Delay>());
    chain.fxPlaceInitial(3, std::make_unique<fx::Hall>());
  }
  chain.finalize();  // partition prefix/suffix + publish FX order
  chain.prepare(sr, block);

  FILE* dump = nullptr;
  if (!outPath.empty()) {
    dump = std::fopen(outPath.c_str(), "wb");
    if (!dump) { printf("cannot open %s\n", outPath.c_str()); return 1; }
  }

  std::vector<float> L(block), R(block);

  if (sweep && drive) {
    // Loudness-compensation + DC check: hold each Drive setting for 3 s,
    // measure the last 2.5 s (skips the smoother glide), print RMS + DC.
    Param* p = drive->param("drive");
    printf("%-8s %-10s %-10s %-10s\n", "drive", "rms dB", "peak", "dc mean");
    double base = 0.0;
    for (float v : {0.0f, 25.0f, 50.0f, 75.0f, 100.0f}) {
      p->set(v);
      const long settle = (long)(sr * 0.5), measure = (long)(sr * 2.5);
      Stats st;
      TestSignal sig(sr, freqHz);
      for (long n = 0; n < settle + measure; n += block) {
        int frames = (int)std::min<long>(block, settle + measure - n);
        for (int i = 0; i < frames; i++) { L[(size_t)i] = R[(size_t)i] = sig.next(n + i); }
        chain.process(L.data(), R.data(), frames);
        if (n >= settle) st.accum(L.data(), frames);
      }
      if (v == 0.0f) base = st.rmsDb();
      printf("%-8.0f %-10.2f %-10.3f %-10.6f  (%+.2f dB vs drive=0)\n",
             v, st.rmsDb(), st.peak, st.mean(), st.rmsDb() - base);
    }
    return 0;
  }

  const double seconds = 10.0;
  const int total = (int)(sr * seconds);
  TestSignal sig(sr, freqHz);
  Stats st;
  auto t0 = std::chrono::steady_clock::now();
  for (int n = 0; n < total; n += block) {
    int frames = std::min(block, total - n);
    for (int i = 0; i < frames; i++) { L[(size_t)i] = R[(size_t)i] = sig.next(n + i); }
    chain.process(L.data(), R.data(), frames);
    st.accum(L.data(), frames);
    if (dump) std::fwrite(L.data(), sizeof(float), (size_t)frames, dump);
  }
  auto t1 = std::chrono::steady_clock::now();
  if (dump) std::fclose(dump);

  double wall = std::chrono::duration<double>(t1 - t0).count();
  double rtf = seconds / wall;
  printf("Processed %.1f s of audio in %.3f s wall  ->  RTF %.2fx %s\n",
         seconds, wall, rtf, rtf > 1.0 ? "(realtime OK)" : "(TOO SLOW)");
  printf("Output: peak %.3f, rms %.3f, dc mean %.6f\n", st.peak, st.rms(), st.mean());
  return (rtf > 1.0 && st.peak > 0.0 && std::isfinite(st.peak)) ? 0 : 1;
}

// dsp_util.h -- tiny real-time DSP helpers for the hand-written effects.
//
// The shared primitives: an RBJ biquad, a one-pole parameter smoother, a
// phase-accumulator LFO, and a one-pole lowpass. All streaming, allocation-
// free, and safe to call on the audio thread. (Collection-specific blocks
// live in effects/drive_dsp.h, delay_dsp.h, reverb_dsp.h, mod_dsp.h,
// pitch_dsp.h.)

#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Transposed-direct-form-II biquad. Set coefficients once (prepare), then call
// process() per sample. Keeps its own state (one per channel instance).
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;

  void reset() noexcept { z1 = z2 = 0; }

  float process(float x) noexcept {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  // RBJ cookbook lowpass.
  void setLowpass(double fc, double fs, double Q = 0.70710678) noexcept {
    double w0 = 2.0 * M_PI * fc / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double alpha = s / (2.0 * Q);
    double a0 = 1.0 + alpha;
    b0 = float((1.0 - c) / 2.0 / a0);
    b1 = float((1.0 - c) / a0);
    b2 = b0;
    a1 = float(-2.0 * c / a0);
    a2 = float((1.0 - alpha) / a0);
  }

  // RBJ cookbook peaking EQ (a bell at fc). All in-place: no allocation, safe to
  // recompute on the audio thread when a knob moves.
  void setPeak(double fc, double fs, double dBgain, double Q = 0.9) noexcept {
    double A = std::pow(10.0, dBgain / 40.0);
    double w0 = 2.0 * M_PI * fc / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double alpha = s / (2.0 * Q);
    double a0 = 1.0 + alpha / A;
    b0 = float((1.0 + alpha * A) / a0);
    b1 = float((-2.0 * c) / a0);
    b2 = float((1.0 - alpha * A) / a0);
    a1 = float((-2.0 * c) / a0);
    a2 = float((1.0 - alpha / A) / a0);
  }

  // RBJ low shelf (slope S=1).
  void setLowShelf(double fc, double fs, double dBgain) noexcept {
    double A = std::pow(10.0, dBgain / 40.0);
    double w0 = 2.0 * M_PI * fc / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double sq = 2.0 * std::sqrt(A) * (s / 2.0 * std::sqrt(2.0));
    double a0 = (A + 1.0) + (A - 1.0) * c + sq;
    b0 = float(A * ((A + 1.0) - (A - 1.0) * c + sq) / a0);
    b1 = float(2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0);
    b2 = float(A * ((A + 1.0) - (A - 1.0) * c - sq) / a0);
    a1 = float(-2.0 * ((A - 1.0) + (A + 1.0) * c) / a0);
    a2 = float(((A + 1.0) + (A - 1.0) * c - sq) / a0);
  }

  // RBJ high shelf (slope S=1).
  void setHighShelf(double fc, double fs, double dBgain) noexcept {
    double A = std::pow(10.0, dBgain / 40.0);
    double w0 = 2.0 * M_PI * fc / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double sq = 2.0 * std::sqrt(A) * (s / 2.0 * std::sqrt(2.0));
    double a0 = (A + 1.0) - (A - 1.0) * c + sq;
    b0 = float(A * ((A + 1.0) + (A - 1.0) * c + sq) / a0);
    b1 = float(-2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0);
    b2 = float(A * ((A + 1.0) + (A - 1.0) * c - sq) / a0);
    a1 = float(2.0 * ((A - 1.0) - (A + 1.0) * c) / a0);
    a2 = float(((A + 1.0) - (A - 1.0) * c - sq) / a0);
  }
};

// One-pole parameter smoother: read the target once per block, call next()
// once per sample. `ms` is the time constant (63% of a step; ~5x to settle).
// Kills the zipper noise of block-rate jumps in gains/mixes/depths. snap() in
// prepare() so the first block doesn't ramp up from zero.
struct Smoother {
  float a = 0.0f, y = 0.0f;

  void prepare(double fs, double ms) noexcept {
    a = float(std::exp(-1.0 / (0.001 * ms * fs)));
  }
  void snap(float v) noexcept { y = v; }
  float next(float target) noexcept {
    y = target + a * (y - target);
    return y;
  }
};

// Phase-accumulator LFO -- the hand-rolled idiom shared by the delay and
// modulation collections (promoted from delay_dsp.h on its third consumer).
// next() returns the phase in [0,1); shape it with sine()/raisedCos().
// setPhase() offsets L/R instances so channels decorrelate.
struct Lfo {
  float phase = 0.0f, inc = 0.0f;

  void setRate(float hz, double fs) noexcept { inc = float(hz / fs); }
  void setPhase(float p) noexcept { phase = p - std::floor(p); }
  float next() noexcept {
    const float p = phase;
    phase += inc;
    if (phase >= 1.0f) phase -= 1.0f;
    return p;
  }
  static float sine(float p) noexcept {
    return std::sin(2.0f * (float)M_PI * p);
  }
  static float raisedCos(float p) noexcept {
    return 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * p));
  }
};

// One-pole lowpass, used as a simple post-distortion tone control.
struct OnePole {
  float a = 0.0f, z = 0.0f;

  void setCutoff(double fc, double fs) noexcept {
    a = float(std::exp(-2.0 * M_PI * fc / fs));
  }
  void reset() noexcept { z = 0.0f; }

  float process(float x) noexcept {
    z = (1.0f - a) * x + a * z;
    return z;
  }
};

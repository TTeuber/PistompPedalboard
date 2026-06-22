// dsp_util.h -- tiny real-time DSP helpers for the hand-written effects.
//
// Just enough for the Drive waveshaper: an RBJ biquad (used as the anti-alias
// lowpass in the 2x oversampler) and a one-pole lowpass (tone control). Both are
// streaming, allocation-free, and safe to call on the audio thread.

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

// 2x oversampler around a sample-wise nonlinearity, to keep waveshaping
// harmonics from aliasing back down. Holds the up/down anti-alias filters for
// ONE channel; instantiate one per channel. `shape` is a sample->sample functor.
struct Oversampler2x {
  Biquad up, down;

  // Cutoff a touch below the base-rate Nyquist, at the 2x rate.
  void prepare(double baseFs) noexcept {
    double os = baseFs * 2.0;
    up.setLowpass(baseFs * 0.45, os);
    down.setLowpass(baseFs * 0.45, os);
    up.reset();
    down.reset();
  }

  template <typename Shaper>
  float process(float x, Shaper shape) noexcept {
    // Zero-stuff upsample (x, 0) with *2 gain compensation, shape, anti-alias,
    // then decimate by keeping the first subsample of each pair.
    float u0 = up.process(2.0f * x);
    float u1 = up.process(0.0f);
    float d0 = down.process(shape(u0));
    float d1 = down.process(shape(u1));
    (void)d1;  // advance the decimation filter's state, but drop this subsample
    return d0;
  }
};

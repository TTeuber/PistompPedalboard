// effects/tuner.h -- a chromatic guitar tuner, first in the chain.
//
// Sits at the very front so it sees the clean DI guitar (before gate/drive/amp
// colour the pitch). It is DISENGAGED by default and costs nothing until you turn
// it on -- the chain skips disabled effects entirely. When engaged it:
//   * copies the mono input into a ring buffer (cheap, RT-safe), and
//   * optionally mutes the output (Mute knob) so you can tune silently on stage.
//
// The actual pitch detection is NOT done on the audio thread. process() only does
// a memcpy-cheap ring write; a normal-priority thread (the UI loop) calls
// analyze() a few times a second, snapshots the ring, and runs YIN -- a standard,
// octave-robust monophonic pitch estimator. analyze() publishes the result (note
// index, octave, cents-off) into atomics the LCD reads. So the real-time contract
// is untouched: no allocation/lock/syscall ever runs on the audio thread here.

#pragma once

#include "../effect.h"
#include "../dsp_util.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {

class Tuner : public Effect {
public:
  Tuner() : Effect("tuner", "Tuner") {
    // Engaging the tuner is the generic per-effect power toggle; start OFF so the
    // pedalboard plays normally until you reach for the tuner.
    enabled.store(false);
    mute_ = addParam("mute", "Mute",     "",   0,   1,   1);    // 1 = silent tune
    ref_  = addParam("ref",  "Ref A",    "Hz", 430, 450, 440);  // concert pitch
  }

  void prepare(double sr, int) override {
    sr_ = sr;
    ring_.assign(kRing, 0.0f);
    buf_.assign(kWindow, 0.0f);
    minLag_ = (int)(sr_ / kMaxHz);   // shortest period we look for (high notes)
    maxLag_ = (int)(sr_ / kMinHz);   // longest period (low notes)
    if (maxLag_ > kWindow - 64) maxLag_ = kWindow - 64;
    diff_.assign(maxLag_ + 1, 0.0);
    cmnd_.assign(maxLag_ + 1, 0.0);
    wpos_.store(0, std::memory_order_relaxed);
    clear();
  }

  // REAL-TIME: just capture the dry input and (optionally) mute. No detection.
  void process(float* L, float* R, int n) noexcept override {
    uint32_t w = wpos_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) ring_[(w + (uint32_t)i) & kMask] = L[i];
    wpos_.store(w + (uint32_t)n, std::memory_order_release);

    if (mute_->get() > 0.5f)
      for (int i = 0; i < n; i++) { L[i] = 0.0f; R[i] = 0.0f; }
  }

  // ---- read-only result API for the LCD (any thread) ----
  bool  engaged()   const noexcept { return enabled.load(std::memory_order_relaxed); }
  float freqHz()    const noexcept { return freqHz_.load(std::memory_order_relaxed); }
  int   noteIndex() const noexcept { return note_.load(std::memory_order_relaxed); }  // 0..11, -1 = none
  int   octave()    const noexcept { return octave_.load(std::memory_order_relaxed); }
  float cents()     const noexcept { return cents_.load(std::memory_order_relaxed); }

  // Run one pitch estimate. Call from a NORMAL thread (UI loop), never the audio
  // thread -- this allocates nothing but is O(maxLag * window) work.
  void analyze() noexcept {
    if (!enabled.load(std::memory_order_relaxed)) { clear(); return; }
    uint32_t w = wpos_.load(std::memory_order_acquire);
    if (w < (uint32_t)kWindow) return;  // not enough captured yet

    // Snapshot the most recent window (oldest -> newest). A few boundary samples
    // may race with the audio thread; harmless for pitch (we re-run continuously).
    for (int i = 0; i < kWindow; i++)
      buf_[i] = ring_[(w - (uint32_t)kWindow + (uint32_t)i) & kMask];

    // Silence gate: don't chase noise when nothing is playing.
    double sumsq = 0.0;
    for (int i = 0; i < kWindow; i++) sumsq += (double)buf_[i] * buf_[i];
    if (std::sqrt(sumsq / kWindow) < 2e-3) { clear(); return; }

    // Gentle low-pass tames the bright upper harmonics of an electric guitar so
    // YIN locks to the fundamental instead of an overtone.
    OnePole lp; lp.setCutoff(1200.0, sr_); lp.reset();
    for (int i = 0; i < kWindow; i++) buf_[i] = lp.process(buf_[i]);

    // --- YIN: difference function over the window, then cumulative-mean-normalise.
    const int W = kWindow - maxLag_;
    diff_[0] = 0.0;
    for (int tau = 1; tau <= maxLag_; tau++) {
      double d = 0.0;
      for (int j = 0; j < W; j++) {
        double dv = (double)buf_[j] - (double)buf_[j + tau];
        d += dv * dv;
      }
      diff_[tau] = d;
    }
    cmnd_[0] = 1.0;
    double running = 0.0;
    for (int tau = 1; tau <= maxLag_; tau++) {
      running += diff_[tau];
      cmnd_[tau] = running > 0.0 ? diff_[tau] * tau / running : 1.0;
    }

    // First dip below the absolute threshold (octave-safe); else the global min.
    int tau = -1;
    for (int t = minLag_; t < maxLag_; t++) {
      if (cmnd_[t] < kYinThresh) {
        while (t + 1 <= maxLag_ && cmnd_[t + 1] < cmnd_[t]) t++;  // descend to the dip
        tau = t; break;
      }
    }
    if (tau < 0) {
      double best = 1e9; int bt = -1;
      for (int t = minLag_; t <= maxLag_; t++)
        if (cmnd_[t] < best) { best = cmnd_[t]; bt = t; }
      if (bt < 0 || best > 0.5) { clear(); return; }  // no confident pitch
      tau = bt;
    }

    // Parabolic interpolation around the dip for sub-sample (sub-cent) accuracy.
    double betterTau = tau;
    if (tau > minLag_ && tau < maxLag_) {
      double s0 = cmnd_[tau - 1], s1 = cmnd_[tau], s2 = cmnd_[tau + 1];
      double denom = s0 + s2 - 2.0 * s1;
      if (denom != 0.0) betterTau = tau + 0.5 * (s0 - s2) / denom;
    }

    double freq = sr_ / betterTau;
    publish(freq);
  }

private:
  void publish(double freq) noexcept {
    double ref = ref_->get();
    double midi = 69.0 + 12.0 * std::log2(freq / ref);
    int nearest = (int)std::lround(midi);
    double cents = (midi - nearest) * 100.0;
    freqHz_.store((float)freq, std::memory_order_relaxed);
    note_.store(((nearest % 12) + 12) % 12, std::memory_order_relaxed);
    octave_.store(nearest / 12 - 1, std::memory_order_relaxed);
    cents_.store((float)cents, std::memory_order_relaxed);
  }

  void clear() noexcept {
    freqHz_.store(0.0f, std::memory_order_relaxed);
    note_.store(-1, std::memory_order_relaxed);
    octave_.store(0, std::memory_order_relaxed);
    cents_.store(0.0f, std::memory_order_relaxed);
  }

  static constexpr int   kRing   = 4096;          // power of two for the mask
  static constexpr uint32_t kMask = kRing - 1;
  static constexpr int   kWindow = 2048;          // analysis window (~43 ms @48k)
  static constexpr double kMinHz = 60.0;          // covers down-tuned low strings
  static constexpr double kMaxHz = 1320.0;        // ~E6, well above the 1st string
  static constexpr double kYinThresh = 0.12;      // YIN dip acceptance

  double sr_ = 48000.0;
  int minLag_ = 36, maxLag_ = 800;
  std::vector<float>  ring_, buf_;
  std::vector<double> diff_, cmnd_;
  std::atomic<uint32_t> wpos_{0};

  // detection results (written by analyze on the UI thread, read by the LCD)
  std::atomic<float> freqHz_{0.0f};
  std::atomic<int>   note_{-1};
  std::atomic<int>   octave_{0};
  std::atomic<float> cents_{0.0f};

  Param *mute_, *ref_;
};

}  // namespace fx

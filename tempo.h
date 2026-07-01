// tempo.h -- shared musical-tempo core for beat-synced effects.
//
// Time-based effects (Delay, Tremolo, ...) can lock their time/rate to the song
// tempo instead of a raw knob. The pieces they share live here:
//   * a stable table of note DIVISIONS (1/4, 1/8 dotted, triplets, ...),
//   * helpers to turn (division, BPM) into milliseconds,
//   * a process-wide BPM source (one board = one tempo), read lock-free by the
//     audio thread via tempo::bpm(), and
//   * TapTempo: turns a series of button taps into a BPM (web/UI thread only).
//
// The BPM value itself is owned by PedalControls (pedal_controls.h), same as
// masterLevel; tempo::setSource() points this accessor at it once at startup so
// effects don't each need a pointer threaded through the chain.

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>

namespace tempo {

// One selectable note value. `beats` is its length in quarter-note beats, so
// ms = beats * (60000 / bpm). The ORDER is the wire contract: the web UI's
// division dropdown mirrors this array by index, so append -- never reorder.
struct Division {
  const char* label;
  float beats;
};

inline constexpr Division kDivisions[] = {
    {"1/1", 4.0f},      // whole
    {"1/2", 2.0f},      // half
    {"1/2.", 3.0f},     // dotted half
    {"1/4", 1.0f},      // quarter
    {"1/4.", 1.5f},     // dotted quarter
    {"1/4T", 2.0f / 3.0f},  // quarter triplet
    {"1/8", 0.5f},      // eighth
    {"1/8.", 0.75f},    // dotted eighth
    {"1/8T", 1.0f / 3.0f},  // eighth triplet
    {"1/16", 0.25f},    // sixteenth
};

inline constexpr int kDivCount = (int)(sizeof(kDivisions) / sizeof(kDivisions[0]));

// Milliseconds of one quarter-note at this tempo.
inline float beatMs(double bpm) noexcept {
  return (float)(60000.0 / (bpm > 0.0 ? bpm : 120.0));
}

// Milliseconds of division[idx] at this tempo. idx is clamped into range so a
// stale/garbage value can never index out of bounds on the audio thread.
inline float divisionMs(int idx, double bpm) noexcept {
  if (idx < 0) idx = 0;
  if (idx >= kDivCount) idx = kDivCount - 1;
  return kDivisions[idx].beats * beatMs(bpm);
}

// ---- Process-wide BPM source ------------------------------------------------
// Set once at startup to the PedalControls::bpm atomic; bpm() then reads it
// lock-free from anywhere (including the audio thread). Falls back to 120 until
// wired, so effects are always safe to construct.
namespace detail {
inline std::atomic<const std::atomic<double>*> g_bpmSrc{nullptr};
}

inline void setSource(const std::atomic<double>* src) noexcept {
  detail::g_bpmSrc.store(src, std::memory_order_relaxed);
}

inline double bpm() noexcept {
  const std::atomic<double>* src = detail::g_bpmSrc.load(std::memory_order_relaxed);
  return src ? src->load(std::memory_order_relaxed) : 120.0;
}

// ---- Tap tempo --------------------------------------------------------------
// Averages the interval between successive taps into a BPM. A gap longer than
// kResetGap starts a fresh series (you walked away and came back). Mutex-guarded
// because taps can arrive on different httplib worker threads; it is human-paced
// and NEVER touched by the audio thread.
class TapTempo {
public:
  // Register a tap "now". Returns the new BPM once at least two taps in a series
  // agree, or 0 if this tap only started (or restarted) a series.
  double tap() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    std::lock_guard<std::mutex> lk(mutex_);
    if (haveLast_) {
      const double dt = duration<double>(now - last_).count();
      last_ = now;
      if (dt > kResetGap || dt <= 0.0) {  // too slow / clock glitch -> restart
        avgInterval_ = 0.0;
        return 0.0;
      }
      // Exponential moving average smooths jitter across a run of taps.
      avgInterval_ = avgInterval_ > 0.0 ? avgInterval_ * 0.5 + dt * 0.5 : dt;
      double bpm = 60.0 / avgInterval_;
      if (bpm < kMinBpm) bpm = kMinBpm;
      if (bpm > kMaxBpm) bpm = kMaxBpm;
      return bpm;
    }
    haveLast_ = true;
    last_ = now;
    avgInterval_ = 0.0;
    return 0.0;
  }

private:
  static constexpr double kResetGap = 2.0;  // seconds; longer gap = new series
  static constexpr double kMinBpm = 20.0;
  static constexpr double kMaxBpm = 300.0;

  std::mutex mutex_;
  std::chrono::steady_clock::time_point last_{};
  double avgInterval_ = 0.0;
  bool haveLast_ = false;
};

}  // namespace tempo

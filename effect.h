// effect.h -- the reorder-ready core of the worship pedalboard.
//
// An Effect is one "pedal" in the chain. The chain (chain.h) is an ordered
// std::vector<std::unique_ptr<Effect>>; "fixed order for now, reorderable later"
// means the order lives in DATA (the vector), not in hardcoded if-blocks. To
// reorder later you shuffle the vector + reorder the web UI list -- no new code
// per effect.
//
// Every Effect self-describes its parameters (a list of Param). That list does
// double duty:
//   * the AUDIO thread reads each Param's atomic value once per block, and
//   * the WEB layer serializes the whole list to JSON (so the browser builds its
//     own controls) and writes new values back -- generically, with zero
//     per-effect server code.
//
// Real-time contract: process() runs on the SCHED_FIFO audio thread. It must not
// allocate, lock, or make syscalls. Do all allocation in prepare().

#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// One named, bounded control value. Written by the web/input threads, read by
// the audio thread; std::atomic makes that lock-free and safe.
struct Param {
  std::string id;    // stable key used by the API/presets, e.g. "drive"
  std::string name;  // human label for the UI, e.g. "Drive"
  std::string unit;  // display unit, e.g. "%", "ms", "dB" ("" if none)
  float min, max, def;
  std::atomic<float> value;

  Param(std::string id_, std::string name_, std::string unit_,
        float mn, float mx, float df)
      : id(std::move(id_)), name(std::move(name_)), unit(std::move(unit_)),
        min(mn), max(mx), def(df), value(df) {}

  float get() const noexcept { return value.load(std::memory_order_relaxed); }
  void set(float v) noexcept {
    value.store(std::clamp(v, min, max), std::memory_order_relaxed);
  }
};

// Which page of the device UI an effect belongs to. The chain is one flat ordered
// vector (signal order), but the UI groups it into Input / FX / Output sections;
// each effect carries its section so a page can list "its" pedals. Hidden = not
// shown as an editable pedal (e.g. the Tuner, which is a full-screen takeover).
enum class Section { Input, Fx, Output, Hidden };

class Effect {
public:
  Effect(std::string type, std::string name)
      : type_id_(std::move(type)), name_(std::move(name)) {}
  virtual ~Effect() = default;

  // UI grouping (set at chain-build time). Defaults to Fx (the movable middle).
  Section section = Section::Fx;

  // Stable identifier for the kind of effect ("drive", "reverb", "amp", ...).
  // Used as the API/preset key, so it must be unique within a chain.
  const std::string& type_id() const noexcept { return type_id_; }
  const std::string& name() const noexcept { return name_; }
  void set_name(std::string n) { name_ = std::move(n); }
  // The factory rewrites this so duplicate instances of one kind stay unique
  // ("reverb", "reverb-2", ...). Must stay unique within a chain (preset/web key).
  void set_type_id(std::string t) { type_id_ = std::move(t); }

  // Allocate/size everything here (NOT in process). Called once before the audio
  // thread goes real-time, and again if the block size grows.
  virtual void prepare(double sampleRate, int maxBlock) = 0;

  // In-place stereo processing of n frames. REAL-TIME: no alloc/lock/syscall.
  // L and R are separate channel buffers (deinterleaved).
  virtual void process(float* L, float* R, int n) noexcept = 0;

  // Effects whose output rings on after the input stops (delay echoes, reverb
  // wash) return true so bypass keeps them processing and lets the tail decay
  // naturally instead of amputating it.
  virtual bool hasTails() const noexcept { return false; }

  // What the chain calls instead of process(): process() plus click-free
  // bypass. Toggling `enabled` never hard-switches the signal -- a linear
  // crossfade (`step` per sample, ~10 ms end to end) moves between dry and wet.
  // Tails effects fade their INPUT instead, staying live while bypassed so
  // echoes/washes ring out (the dry passes through untouched alongside).
  // dryL/dryR are chain-owned scratch buffers (>= n). REAL-TIME safe.
  void run(float* L, float* R, int n, float* dryL, float* dryR,
           float step) noexcept {
    const bool on = enabled.load(std::memory_order_relaxed);
    const float target = on ? 1.0f : 0.0f;
    if (on ? fade_ >= 1.0f : fade_ <= 0.0f) {  // ramp already settled
      if (on) { process(L, R, n); return; }    // fully engaged
      if (!hasTails()) return;                 // fully bypassed
    }
    for (int i = 0; i < n; i++) { dryL[i] = L[i]; dryR[i] = R[i]; }
    const float f0 = fade_;
    float f = f0;
    if (hasTails()) {
      // Fade the input feeding the effect; its wet tail stays at full level.
      for (int i = 0; i < n; i++) {
        f = stepToward(f, target, step);
        L[i] *= f;
        R[i] *= f;
      }
      process(L, R, n);
      // Re-walk the identical ramp to blend the dry path back in on top.
      f = f0;
      for (int i = 0; i < n; i++) {
        f = stepToward(f, target, step);
        L[i] += dryL[i] * (1.0f - f);
        R[i] += dryR[i] * (1.0f - f);
      }
    } else {
      process(L, R, n);
      for (int i = 0; i < n; i++) {
        f = stepToward(f, target, step);
        L[i] = dryL[i] + (L[i] - dryL[i]) * f;
        R[i] = dryR[i] + (R[i] - dryR[i]) * f;
      }
    }
    fade_ = f;
  }

  // Jump the bypass fade to match `enabled` with no ramp. The chain calls this
  // at prepare time so a pedal that starts disabled (e.g. the tuner) doesn't
  // spend its first 10 ms audibly fading out.
  void snapFade() noexcept {
    fade_ = enabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
  }

  // true = engaged. The chain crossfades disabled effects out (see run()).
  // Lock-free toggle.
  std::atomic<bool> enabled{true};

  // Footswitch assignment (phase 3). fsAssign = which footswitch (0..3) toggles
  // this effect, or -1 for none. fsMode = 0 normal (on when the FS is engaged)
  // or 1 inverted (on when it is NOT). Written by the UI thread, read anywhere;
  // the UI thread keeps `enabled` in sync with the bound footswitch's state.
  std::atomic<int> fsAssign{-1};
  std::atomic<int> fsMode{0};

  std::vector<std::unique_ptr<Param>> params;

  Param* param(const std::string& id) {
    for (auto& p : params)
      if (p->id == id) return p.get();
    return nullptr;
  }

protected:
  // Declare a parameter from a subclass constructor; returns a raw pointer the
  // subclass keeps for fast access in process() (the Param outlives it).
  Param* addParam(std::string id, std::string name, std::string unit,
                  float mn, float mx, float df) {
    params.push_back(std::make_unique<Param>(std::move(id), std::move(name),
                                             std::move(unit), mn, mx, df));
    return params.back().get();
  }

private:
  // Move v toward target by at most `step`, without overshooting.
  static float stepToward(float v, float target, float step) noexcept {
    if (v < target) return std::min(v + step, target);
    if (v > target) return std::max(v - step, target);
    return v;
  }

  std::string type_id_;
  std::string name_;
  float fade_ = 1.0f;  // bypass crossfade position: 1 = wet, 0 = dry (audio thread only)
};

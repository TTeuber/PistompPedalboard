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

class Effect {
public:
  Effect(std::string type, std::string name)
      : type_id_(std::move(type)), name_(std::move(name)) {}
  virtual ~Effect() = default;

  // Stable identifier for the kind of effect ("drive", "reverb", "amp", ...).
  // Used as the API/preset key, so it must be unique within a chain.
  const std::string& type_id() const noexcept { return type_id_; }
  const std::string& name() const noexcept { return name_; }
  void set_name(std::string n) { name_ = std::move(n); }

  // Allocate/size everything here (NOT in process). Called once before the audio
  // thread goes real-time, and again if the block size grows.
  virtual void prepare(double sampleRate, int maxBlock) = 0;

  // In-place stereo processing of n frames. REAL-TIME: no alloc/lock/syscall.
  // L and R are separate channel buffers (deinterleaved).
  virtual void process(float* L, float* R, int n) noexcept = 0;

  // true = engaged. The chain skips disabled effects. Lock-free toggle.
  std::atomic<bool> enabled{true};

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
  std::string type_id_;
  std::string name_;
};

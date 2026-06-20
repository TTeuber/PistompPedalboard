// chain.h -- the ordered effect chain the audio thread runs each block.
//
// Owns the effects as an ordered vector (the order IS the signal-chain order).
// add() appends in classic order at startup; process() walks the vector in
// order, skipping disabled effects. Runtime reordering is deferred (see plan):
// when wanted, guard the vector with a seqlock/RCU swap so the audio thread never
// sees a half-swapped vector. For now the vector is built once and only the
// per-effect atomics change at runtime, which is already lock-free.

#pragma once

#include "effect.h"

#include <memory>
#include <vector>

class Chain {
public:
  // Append an effect (transfers ownership). Returns a borrowed pointer so the
  // caller can wire presets/UI to it.
  Effect* add(std::unique_ptr<Effect> fx) {
    effects_.push_back(std::move(fx));
    return effects_.back().get();
  }

  // Size every effect's buffers before going real-time.
  void prepare(double sampleRate, int maxBlock) {
    for (auto& fx : effects_) fx->prepare(sampleRate, maxBlock);
  }

  // REAL-TIME: run the in-place stereo signal through each enabled effect.
  void process(float* L, float* R, int n) noexcept {
    for (auto& fx : effects_)
      if (fx->enabled.load(std::memory_order_relaxed))
        fx->process(L, R, n);
  }

  // Lookup by type_id (unique within the chain). Used by the web/preset layer.
  Effect* find(const std::string& typeId) {
    for (auto& fx : effects_)
      if (fx->type_id() == typeId) return fx.get();
    return nullptr;
  }

  const std::vector<std::unique_ptr<Effect>>& effects() const { return effects_; }

private:
  std::vector<std::unique_ptr<Effect>> effects_;
};

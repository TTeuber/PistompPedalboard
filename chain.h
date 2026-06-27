// chain.h -- the ordered effect chain the audio thread runs each block.
//
// The chain has three regions, in signal order:
//   prefix  -- fixed: the Hidden tuner tap + the Input-section effects.
//   FX      -- the reorderable middle: a fixed grid of kFxSlots positional
//              slots, each empty or owning an effect instance (duplicates of a
//              kind are allowed -- instances come from FxFactory).
//   suffix  -- fixed: the Output-section effects (amp, eq).
//
// prefix/suffix singletons are added with add() and partitioned by Section in
// finalize(). FX instances live in fxSlots_ -- pre-placed at build time with
// fxPlaceInitial(), then added/removed/moved at runtime. Signal flows through
// the occupied slots in slot-index order; empty slots are skipped.
//
// Real-time reorder (RCU-lite): the audio thread reaches the active FX list
// through a single std::atomic<const FxOrder*>. The UI thread edits by building
// a NEW FxOrder snapshot and atomically publishing it, so the audio thread
// never sees a half-built order. Anything unpublished by an edit (the previous
// order, plus any effect removed/replaced) is retired and freed on the NEXT
// edit -- safe because edits are human-paced (seconds) while the audio thread
// re-reads the pointer every block (~1.3 ms), so retired objects are provably
// unreferenced long before they're freed. No alloc/free ever happens on the
// audio thread.

#pragma once

#include "effect.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// An immutable snapshot of the active FX (middle) region, in signal order.
// Published to the audio thread by pointer; never mutated after creation.
struct FxOrder {
  std::vector<Effect*> slots;
};

class Chain {
public:
  static constexpr int kFxSlots = 8;   // 4x2 grid

  // Append a prefix/suffix singleton (transfers ownership). Returns a borrowed
  // pointer so the caller can set its Section and wire presets/UI to it.
  Effect* add(std::unique_ptr<Effect> fx) {
    effects_.push_back(std::move(fx));
    return effects_.back().get();
  }

  // Pre-place an FX instance into a grid slot at BUILD time (before prepare/RT).
  Effect* fxPlaceInitial(int slot, std::unique_ptr<Effect> fx) {
    if (slot < 0 || slot >= kFxSlots) return nullptr;
    fxSlots_[(size_t)slot] = std::move(fx);
    return fxSlots_[(size_t)slot].get();
  }

  // Call ONCE after the build-time layout (before going real-time). Partitions
  // the singletons into prefix (Hidden/Input) and suffix (Output) -- preserving
  // add order -- and publishes the initial FX order from the placed slots.
  void finalize() {
    prefix_.clear(); suffix_.clear();
    for (auto& fx : effects_) {
      if (fx->section == Section::Output) suffix_.push_back(fx.get());
      else prefix_.push_back(fx.get());   // Input/Hidden (FX never live here)
    }
    commit();
  }

  // Size every effect's buffers before going real-time, and remember the format
  // so runtime-created FX instances can be prepared off the audio thread.
  void prepare(double sampleRate, int maxBlock) {
    sr_ = sampleRate; maxBlock_ = maxBlock;
    for (auto& fx : effects_) fx->prepare(sampleRate, maxBlock);
    for (auto& s : fxSlots_) if (s) s->prepare(sampleRate, maxBlock);
  }

  // REAL-TIME: run the in-place stereo signal through prefix, the published FX
  // order, then suffix -- skipping disabled effects.
  void process(float* L, float* R, int n) noexcept {
    for (auto* fx : prefix_)
      if (fx->enabled.load(std::memory_order_relaxed)) fx->process(L, R, n);
    if (const FxOrder* o = fxOrder_.load(std::memory_order_acquire))
      for (auto* fx : o->slots)
        if (fx->enabled.load(std::memory_order_relaxed)) fx->process(L, R, n);
    for (auto* fx : suffix_)
      if (fx->enabled.load(std::memory_order_relaxed)) fx->process(L, R, n);
  }

  // Lookup by type_id (unique within the chain). Used by the web/preset layer.
  Effect* find(const std::string& typeId) {
    std::lock_guard<std::mutex> lk(editMutex_);
    for (auto& fx : effects_)
      if (fx->type_id() == typeId) return fx.get();
    for (auto& s : fxSlots_)
      if (s && s->type_id() == typeId) return s.get();
    return nullptr;
  }

  // Snapshot of the whole chain in signal order (prefix, active FX, suffix).
  // Used by the web/preset layer; safe on any non-audio thread.
  std::vector<Effect*> effects() const {
    std::lock_guard<std::mutex> lk(editMutex_);
    std::vector<Effect*> out;
    out.reserve(prefix_.size() + kFxSlots + suffix_.size());
    for (auto* fx : prefix_) out.push_back(fx);
    for (auto& s : fxSlots_) if (s) out.push_back(s.get());
    for (auto* fx : suffix_) out.push_back(fx);
    return out;
  }

  // ---- FX grid editing -- UI THREAD ONLY -----------------------------------
  static constexpr int fxSlotCount() { return kFxSlots; }

  bool fxOccupied(int slot) const {
    std::lock_guard<std::mutex> lk(editMutex_);
    return slot >= 0 && slot < kFxSlots && fxSlots_[(size_t)slot] != nullptr;
  }
  Effect* fxAt(int slot) const {
    std::lock_guard<std::mutex> lk(editMutex_);
    return (slot >= 0 && slot < kFxSlots) ? fxSlots_[(size_t)slot].get() : nullptr;
  }
  int fxSlotOf(Effect* fx) const {
    std::lock_guard<std::mutex> lk(editMutex_);
    for (int i = 0; i < kFxSlots; i++)
      if (fxSlots_[(size_t)i].get() == fx) return i;
    return -1;
  }

  // Snapshot of the grid as type_ids ("" = empty slot), consistent under one
  // lock. Used by the web layer to render the 8-slot grid.
  std::array<std::string, kFxSlots> fxGrid() const {
    std::lock_guard<std::mutex> lk(editMutex_);
    std::array<std::string, kFxSlots> out;
    for (int i = 0; i < kFxSlots; i++)
      out[(size_t)i] = fxSlots_[(size_t)i] ? fxSlots_[(size_t)i]->type_id()
                                           : std::string{};
    return out;
  }

  // Place a freshly-created (UNprepared) instance into a slot, replacing any
  // current occupant. Prepares it here (UI thread) before it becomes visible
  // to the audio thread.
  void fxPlace(int slot, std::unique_ptr<Effect> fx) {
    if (slot < 0 || slot >= kFxSlots || !fx) return;
    fx->prepare(sr_, maxBlock_);
    std::lock_guard<std::mutex> lk(editMutex_);
    auto old = std::move(fxSlots_[(size_t)slot]);   // may be null
    fxSlots_[(size_t)slot] = std::move(fx);
    commit(std::move(old));
  }

  // Empty a slot (its effect stops processing; freed after the grace period).
  void fxRemove(int slot) {
    std::lock_guard<std::mutex> lk(editMutex_);
    if (slot < 0 || slot >= kFxSlots || !fxSlots_[(size_t)slot]) return;
    auto removed = std::move(fxSlots_[(size_t)slot]);
    fxSlots_[(size_t)slot] = nullptr;
    commit(std::move(removed));
  }

  // Swap a slot with its neighbor toward the front (dir < 0) or back (dir > 0).
  void fxMove(int slot, int dir) {
    int j = slot + (dir < 0 ? -1 : 1);
    if (slot < 0 || slot >= kFxSlots || j < 0 || j >= kFxSlots) return;
    std::lock_guard<std::mutex> lk(editMutex_);
    std::swap(fxSlots_[(size_t)slot], fxSlots_[(size_t)j]);
    commit();
  }

  // Relocate the effect in `fromSlot` to sit just before whatever occupies
  // `toSlot`, then repack so the chain has no gaps -- the "insert & compact"
  // drag-and-drop primitive. A `toSlot` that is empty (or out of range) appends
  // the effect to the end of the chain.
  void fxReorder(int fromSlot, int toSlot) {
    std::lock_guard<std::mutex> lk(editMutex_);
    if (fromSlot < 0 || fromSlot >= kFxSlots || !fxSlots_[(size_t)fromSlot]) return;

    // Pull the occupied slots into signal order, tracking where the dragged
    // effect and the drop target landed within that compacted sequence.
    std::vector<std::unique_ptr<Effect>> seq;
    int fromIdx = -1, toIdx = -1;
    for (int i = 0; i < kFxSlots; i++) {
      if (!fxSlots_[(size_t)i]) continue;
      if (i == fromSlot) fromIdx = (int)seq.size();
      if (i == toSlot) toIdx = (int)seq.size();
      seq.push_back(std::move(fxSlots_[(size_t)i]));
    }

    auto moved = std::move(seq[(size_t)fromIdx]);
    seq.erase(seq.begin() + fromIdx);
    // After erasing the source, indices past it shift left by one; an unknown
    // (empty/out-of-range) target appends.
    int insert = (toIdx < 0) ? (int)seq.size() : (toIdx > fromIdx ? toIdx - 1 : toIdx);
    seq.insert(seq.begin() + insert, std::move(moved));

    // Repack into the low slots; null the rest.
    for (int i = 0; i < kFxSlots; i++)
      fxSlots_[(size_t)i] = (i < (int)seq.size()) ? std::move(seq[(size_t)i]) : nullptr;
    commit();
  }

private:
  // Build a fresh immutable order from the slots and swap it in for the audio
  // thread. First reclaims everything retired by the PREVIOUS edit (provably
  // unreferenced by now), then retires this edit's old order plus an optional
  // removed/replaced effect (now unpublished, freed on the next edit).
  void commit(std::unique_ptr<Effect> retiredEffect = nullptr) {
    fxRetiredOrders_.clear();
    fxRetiredEffects_.clear();

    auto next = std::make_unique<FxOrder>();
    for (auto& s : fxSlots_) if (s) next->slots.push_back(s.get());

    if (fxLive_) fxRetiredOrders_.push_back(std::move(fxLive_));
    fxLive_ = std::move(next);
    fxOrder_.store(fxLive_.get(), std::memory_order_release);

    if (retiredEffect) fxRetiredEffects_.push_back(std::move(retiredEffect));
  }

  std::vector<std::unique_ptr<Effect>> effects_;        // owns prefix/suffix singletons
  std::vector<Effect*> prefix_, suffix_;                // fixed regions (raw views)
  std::array<std::unique_ptr<Effect>, kFxSlots> fxSlots_{};  // owns FX instances

  double sr_ = 0; int maxBlock_ = 0;                    // format for runtime prepare()

  std::atomic<const FxOrder*> fxOrder_{nullptr};        // published to audio thread
  std::unique_ptr<const FxOrder> fxLive_;               // currently published (owned)
  std::vector<std::unique_ptr<const FxOrder>> fxRetiredOrders_;   // awaiting reclaim
  std::vector<std::unique_ptr<Effect>> fxRetiredEffects_;         // awaiting reclaim

  // Serializes grid edits + structural reads across the UI and web threads (now
  // that both can mutate the grid). The AUDIO thread never touches this -- it
  // reaches the FX list only through the atomic fxOrder_, so it stays lock-free.
  mutable std::mutex editMutex_;
};

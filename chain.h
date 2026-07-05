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
// Real-time reorder (RCU with an epoch grace period): the audio thread reaches
// the active FX list through a single std::atomic<const FxOrder*>. The UI/web
// threads edit by building a NEW FxOrder snapshot and atomically publishing it,
// so the audio thread never sees a half-built order. Reclamation is driven by
// a block counter the audio thread bumps at the end of every process() call:
// anything unpublished by an edit (the previous order, plus any effect
// removed/replaced) is retired stamped "safe at epoch e+2" and freed only once
// the counter has actually advanced past that -- so even a BURST of edits
// (a rig load replacing all 8 slots back-to-back) can never free an order or
// effect the in-flight audio block is still reading. No alloc/free ever
// happens on the audio thread.
//
// Effect lifetime beyond the audio thread: grid instances are held by
// shared_ptr, and find()/fxAt()/effects() hand out shared_ptr references. A
// web handler or UI page that is still looking at a pedal when another thread
// removes it keeps the object alive (stale but valid) instead of dangling.

#pragma once

#include "effect.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
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
  // pointer so the caller can set its Section and wire presets/UI to it --
  // safe to keep raw because singletons live as long as the chain.
  Effect* add(std::unique_ptr<Effect> fx) {
    effects_.push_back(std::shared_ptr<Effect>(std::move(fx)));
    return effects_.back().get();
  }

  // Pre-place an FX instance into a grid slot at BUILD time (before prepare/RT).
  Effect* fxPlaceInitial(int slot, std::unique_ptr<Effect> fx) {
    if (slot < 0 || slot >= kFxSlots) return nullptr;
    fxSlots_[(size_t)slot] = std::shared_ptr<Effect>(std::move(fx));
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
  // so runtime-created FX instances can be prepared off the audio thread. The
  // audio thread is guaranteed stopped here, so it's also the one safe point to
  // drop retired entries unconditionally (the block counter isn't advancing, so
  // epoch-gated reclaim alone would hold them until the next edit-after-audio).
  // Takes the edit lock: the web thread can be mid-grid-edit when a device
  // reconnect re-prepares the chain.
  void prepare(double sampleRate, int maxBlock) {
    std::lock_guard<std::mutex> lk(editMutex_);
    sr_ = sampleRate; maxBlock_ = maxBlock;
    retired_.clear();
    fadeDryL_.assign((size_t)maxBlock, 0.0f);   // bypass-crossfade scratch
    fadeDryR_.assign((size_t)maxBlock, 0.0f);
    fadeStep_ = float(1.0 / (kFadeMs * 0.001 * sampleRate));
    for (auto& fx : effects_) { fx->prepare(sampleRate, maxBlock); fx->snapFade(); }
    for (auto& s : fxSlots_) if (s) { s->prepare(sampleRate, maxBlock); s->snapFade(); }
  }

  // REAL-TIME: run the in-place stereo signal through prefix, the published FX
  // order, then suffix. Effect::run() handles engaged/bypassed per effect --
  // crossfading toggles so footswitches never click, and letting delay/reverb
  // tails ring out while bypassed. The closing epoch bump (release) publishes
  // "this block is done with whatever order it read" to the reclaimer.
  void process(float* L, float* R, int n) noexcept {
    float* dl = fadeDryL_.data();
    float* dr = fadeDryR_.data();
    for (auto* fx : prefix_) fx->run(L, R, n, dl, dr, fadeStep_);
    if (const FxOrder* o = fxOrder_.load(std::memory_order_acquire))
      for (auto* fx : o->slots) fx->run(L, R, n, dl, dr, fadeStep_);
    for (auto* fx : suffix_) fx->run(L, R, n, dl, dr, fadeStep_);
    epoch_.fetch_add(1, std::memory_order_release);
  }

  // Lookup by type_id (unique within the chain). Used by the web/preset/UI
  // layers; the returned shared_ptr keeps the effect alive across grid edits.
  std::shared_ptr<Effect> find(const std::string& typeId) {
    std::lock_guard<std::mutex> lk(editMutex_);
    for (auto& fx : effects_)
      if (fx->type_id() == typeId) return fx;
    for (auto& s : fxSlots_)
      if (s && s->type_id() == typeId) return s;
    return nullptr;
  }

  // Snapshot of the whole chain in signal order (prefix, active FX, suffix).
  // Used by the web/preset layer; safe on any non-audio thread.
  std::vector<std::shared_ptr<Effect>> effects() const {
    std::lock_guard<std::mutex> lk(editMutex_);
    std::vector<std::shared_ptr<Effect>> out;
    out.reserve(effects_.size() + kFxSlots);
    for (const auto& fx : effects_)          // prefix: Input/Hidden, add order
      if (fx->section != Section::Output) out.push_back(fx);
    for (const auto& s : fxSlots_) if (s) out.push_back(s);
    for (const auto& fx : effects_)          // suffix: Output, add order
      if (fx->section == Section::Output) out.push_back(fx);
    return out;
  }

  // ---- FX grid editing -- UI + web threads (serialized by editMutex_) -------
  static constexpr int fxSlotCount() { return kFxSlots; }

  bool fxOccupied(int slot) const {
    std::lock_guard<std::mutex> lk(editMutex_);
    return slot >= 0 && slot < kFxSlots && fxSlots_[(size_t)slot] != nullptr;
  }
  std::shared_ptr<Effect> fxAt(int slot) const {
    std::lock_guard<std::mutex> lk(editMutex_);
    return (slot >= 0 && slot < kFxSlots) ? fxSlots_[(size_t)slot] : nullptr;
  }
  int fxSlotOf(const Effect* fx) const {
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
    double sr; int mb;
    {  // snapshot the format under the lock (prepare() can rewrite it)
      std::lock_guard<std::mutex> lk(editMutex_);
      sr = sr_; mb = maxBlock_;
    }
    fx->prepare(sr, mb);   // allocation-heavy: deliberately outside the lock
    fx->snapFade();
    std::lock_guard<std::mutex> lk(editMutex_);
    auto old = std::move(fxSlots_[(size_t)slot]);   // may be null
    fxSlots_[(size_t)slot] = std::shared_ptr<Effect>(std::move(fx));
    commit(std::move(old));
  }

  // Empty a slot (its effect stops processing; freed after the grace period,
  // or later still if a web/UI thread is holding a reference).
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

  // Free-form drag-and-drop: drop the effect from `fromSlot` onto `toSlot`,
  // keeping every effect at its exact cell so the grid can have gaps. If the
  // target is empty the effect simply moves there (vacating its old cell); if
  // occupied, the two swap. No repacking -- positions are preserved as the user
  // arranged them. Signal flows in slot-index order, skipping the empty cells.
  void fxMoveTo(int fromSlot, int toSlot) {
    if (fromSlot < 0 || fromSlot >= kFxSlots ||
        toSlot < 0 || toSlot >= kFxSlots || fromSlot == toSlot)
      return;
    std::lock_guard<std::mutex> lk(editMutex_);
    if (!fxSlots_[(size_t)fromSlot]) return;        // nothing to move
    std::swap(fxSlots_[(size_t)fromSlot], fxSlots_[(size_t)toSlot]);
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
    std::vector<std::shared_ptr<Effect>> seq;
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
  // One retired object awaiting reclamation: freeable once the audio thread's
  // block counter reaches safeEpoch. Either field may be null.
  struct Retired {
    uint64_t safeEpoch;
    std::unique_ptr<const FxOrder> order;
    std::shared_ptr<Effect> fx;
  };

  // Build a fresh immutable order from the slots and swap it in for the audio
  // thread. Reclaims whatever the audio thread has provably moved past, then
  // retires this edit's old order plus an optional removed/replaced effect.
  //
  // Grace-period reasoning: the store of the new order happens BEFORE the
  // retire epoch is stamped. At that point, at most the one audio block
  // currently in flight can still hold the old pointer; it finishes at epoch
  // e+1, and every later block re-reads fxOrder_ and gets the new order. We
  // stamp e+2 (one extra full block, ~1.3 ms of margin) and free only once the
  // counter actually gets there -- so back-to-back edits (a rig load) can
  // never free something inside its grace window. If audio is stopped or
  // bypassed the counter stalls and entries simply wait (bounded by edit
  // count; prepare() clears them at the next audio-stopped point).
  void commit(std::shared_ptr<Effect> retiredEffect = nullptr) {
    const uint64_t now = epoch_.load(std::memory_order_acquire);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [now](const Retired& r) {
                                    return now >= r.safeEpoch;
                                  }),
                   retired_.end());

    auto next = std::make_unique<FxOrder>();
    for (auto& s : fxSlots_) if (s) next->slots.push_back(s.get());

    fxOrder_.store(next.get(), std::memory_order_release);
    // StoreLoad fence: the epoch MUST be sampled at-or-after the publish above
    // is visible. Without it the load could be hoisted before the store, and a
    // stamp taken blocks earlier would already be expired at retire time.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const uint64_t e = epoch_.load(std::memory_order_relaxed);
    if (fxLive_) retired_.push_back({e + 2, std::move(fxLive_), nullptr});
    fxLive_ = std::move(next);
    if (retiredEffect)
      retired_.push_back({e + 2, nullptr, std::move(retiredEffect)});
  }

  std::vector<std::shared_ptr<Effect>> effects_;    // owns prefix/suffix singletons
  std::vector<Effect*> prefix_, suffix_;            // fixed regions (raw views)
  std::array<std::shared_ptr<Effect>, kFxSlots> fxSlots_{};  // owns FX instances

  double sr_ = 0; int maxBlock_ = 0;                // format for runtime prepare()

  // Bypass-crossfade support (see Effect::run). The scratch buffers hold each
  // effect's dry input during a fade; safe to share across effects because the
  // chain is strictly serial. Audio thread only.
  static constexpr double kFadeMs = 10.0;           // toggle crossfade length
  std::vector<float> fadeDryL_, fadeDryR_;
  float fadeStep_ = 1.0f;                           // fade progress per sample

  std::atomic<const FxOrder*> fxOrder_{nullptr};    // published to audio thread
  std::atomic<uint64_t> epoch_{0};                  // blocks completed (audio thread)
  std::unique_ptr<const FxOrder> fxLive_;           // currently published (owned)
  std::vector<Retired> retired_;                    // awaiting their safeEpoch

  // Serializes grid edits + structural reads across the UI and web threads (now
  // that both can mutate the grid). The AUDIO thread never touches this -- it
  // reaches the FX list only through the atomic fxOrder_, so it stays lock-free.
  mutable std::mutex editMutex_;
};

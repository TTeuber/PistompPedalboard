// test_chain_stress.cpp -- the audio thread vs. concurrent grid edits.
//
// One thread simulates the SCHED_FIFO audio thread (chain.process() flat out,
// no sleeps); two editor threads hammer every grid mutation the UI/web layers
// can issue -- place/remove/move/reorder, rig-load-style full-grid replacement
// bursts, and shared_ptr reads with param writes. This is the test that would
// have caught the FX-order reclamation bug: the epoch grace period must never
// free an FxOrder or Effect an in-flight block is still reading.
//
// On a plain build it mostly proves "no crash, audio stays finite". Its real
// teeth are the sanitizer configs (scripts/test-mac.sh tsan|asan): TSan checks
// the RCU publish/retire protocol's happens-before edges, ASan catches any
// use-after-free the grace period misses.

#include "chain.h"
#include "effects/input_gain.h"
#include "effects/output_gain.h"
#include "fx_factory.h"
#include "test_util.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using testutil::kBlock;
using testutil::kRate;

TEST_CASE("concurrent grid edits never corrupt the audio path",
          "[chain][stress]") {
  FxFactory f;
  registerAllFx(f);
  Chain chain;
  chain.add(std::make_unique<fx::InputGain>())->section = Section::Input;
  chain.add(std::make_unique<fx::OutputGain>())->section = Section::Output;
  chain.fxPlaceInitial(0, f.create("overdrive"));
  chain.fxPlaceInitial(1, f.create("delay"));
  chain.finalize();
  chain.prepare(kRate, kBlock);

  std::atomic<bool> stop{false};
  std::atomic<bool> audioClean{true};
  std::atomic<uint64_t> blocksDone{0};

  // Audio-thread simulator: process continuously until the editors finish.
  // No Catch2 assertions here (they are not thread-safe); just record.
  std::thread audio([&] {
    std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
    testutil::SignalGen gen;
    while (!stop.load(std::memory_order_relaxed)) {
      gen.fill(L.data(), R.data(), kBlock);
      chain.process(L.data(), R.data(), kBlock);
      if (testutil::firstBad(L.data(), kBlock, 1.0e6f) >= 0 ||
          testutil::firstBad(R.data(), kBlock, 1.0e6f) >= 0)
        audioClean.store(false, std::memory_order_relaxed);
      blocksDone.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Editors: every mutation the UI/web threads can issue, in a random hammer.
  auto editor = [&](uint32_t seed, int ops) {
    std::mt19937 rng(seed);
    auto slot = [&] { return (int)(rng() % (unsigned)Chain::kFxSlots); };
    auto kind = [&] { return (size_t)(rng() % f.kinds().size()); };
    for (int i = 0; i < ops; i++) {
      switch (rng() % 10u) {
        case 0:
        case 1:  // place a fresh instance (prepare + publish + retire old)
          if (auto fx = f.create(kind())) chain.fxPlace(slot(), std::move(fx));
          break;
        case 2:
          chain.fxRemove(slot());
          break;
        case 3:
          chain.fxMove(slot(), (rng() % 2u) ? +1 : -1);
          break;
        case 4:
          chain.fxMoveTo(slot(), slot());
          break;
        case 5:
          chain.fxReorder(slot(), slot());
          break;
        case 6:  // rig-load burst: replace the WHOLE grid back-to-back --
                 // the exact pattern that overwhelmed the old reclamation
          for (int s = 0; s < Chain::kFxSlots; s++)
            if (auto fx = f.create(kind())) chain.fxPlace(s, std::move(fx));
          break;
        case 7:  // web-style reader: snapshot + param writes on held refs
          for (const auto& fx : chain.effects())
            for (const auto& p : fx->params)
              p->set(p->min + 0.5f * (p->max - p->min));
          break;
        case 8: {  // UI-style reader: grid snapshot + bypass toggle
          (void)chain.fxGrid();
          if (auto fx = chain.fxAt(slot()))
            fx->enabled.store(!fx->enabled.load());
          break;
        }
        case 9: {  // lookup by id + footswitch assign
          if (auto fx = chain.find("delay"))
            fx->fsAssign.store((int)(rng() % 5u) - 1);
          break;
        }
      }
    }
  };
  // Op count sized so the plain build soaks ~0.2 s and the TSan build a few
  // seconds -- enough interleavings to matter, short enough for every run.
  std::thread e1(editor, 0xA5A5A5A5u, 4000);
  std::thread e2(editor, 0x5EED5EEDu, 4000);
  e1.join();
  e2.join();
  stop.store(true);
  audio.join();

  CHECK(audioClean.load());
  CHECK(blocksDone.load() > 0);  // the audio thread really ran throughout

  // The grid must come out consistent: occupied slots hold unique, non-empty
  // ids (the factory's suffixing survived two threads minting concurrently).
  std::set<std::string> ids;
  for (const std::string& id : chain.fxGrid()) {
    if (id.empty()) continue;
    CAPTURE(id);
    CHECK(ids.insert(id).second);
  }

  // And the chain still processes cleanly after the storm.
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
  testutil::SignalGen gen;
  for (int b = 0; b < 50; b++) {
    gen.fill(L.data(), R.data(), kBlock);
    chain.process(L.data(), R.data(), kBlock);
    REQUIRE(testutil::firstBad(L.data(), kBlock, 1.0e6f) < 0);
    REQUIRE(testutil::firstBad(R.data(), kBlock, 1.0e6f) < 0);
  }
}

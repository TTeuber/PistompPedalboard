// test_chain_grid.cpp -- FX grid editing semantics: fxMoveTo (position-
// preserving swap/move), fxReorder (insert-before + repack), fxMove (neighbor
// swap), fxPlace/fxRemove, and the chain-order view the preset/web layers use.
//
// Layouts are asserted through fxGrid() (type_ids per slot, "" = empty), which
// is also exactly what rigs serialize. Distinct kinds are used so ids == kinds.

#include "chain.h"
#include "effects/input_gain.h"
#include "effects/output_gain.h"
#include "fx_factory.h"
#include "test_util.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

using testutil::kBlock;
using testutil::kRate;

namespace {

// Grid layout as a fixed array for easy comparison; "" = empty slot.
using Layout = std::array<std::string, Chain::kFxSlots>;

Layout layoutOf(const Chain& c) { return c.fxGrid(); }

Layout expect(std::vector<std::string> slots) {
  Layout out{};
  for (size_t i = 0; i < slots.size() && i < out.size(); i++) out[i] = slots[i];
  return out;
}

// A prepared chain with the given kinds pre-placed ("" = leave slot empty).
// The caller owns the factory so follow-up fxPlace edits share its counters.
void build(Chain& chain, FxFactory& f, const std::vector<std::string>& kinds) {
  for (size_t i = 0; i < kinds.size(); i++)
    if (!kinds[i].empty()) chain.fxPlaceInitial((int)i, f.create(kinds[i]));
  chain.finalize();
  chain.prepare(kRate, kBlock);
}

}  // namespace

TEST_CASE("fxMoveTo moves into gaps and swaps occupants, preserving cells",
          "[chain][grid]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  build(chain, f, {"overdrive", "", "delay", "", "", "hall"});

  SECTION("drop onto an empty cell vacates the source (gap preserved)") {
    chain.fxMoveTo(0, 1);
    CHECK(layoutOf(chain) == expect({"", "overdrive", "delay", "", "", "hall"}));
  }
  SECTION("drop onto an occupied cell swaps the two") {
    chain.fxMoveTo(0, 2);
    CHECK(layoutOf(chain) == expect({"delay", "", "overdrive", "", "", "hall"}));
  }
  SECTION("no-ops: empty source, same slot, out of range") {
    const Layout before = layoutOf(chain);
    chain.fxMoveTo(1, 0);    // empty source
    chain.fxMoveTo(2, 2);    // from == to
    chain.fxMoveTo(-1, 0);   // out of range
    chain.fxMoveTo(0, Chain::kFxSlots);
    CHECK(layoutOf(chain) == before);
  }
}

TEST_CASE("fxReorder inserts before the target and repacks", "[chain][grid]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  build(chain, f, {"overdrive", "", "delay", "", "", "hall"});

  SECTION("drag back to the front compacts the gaps away") {
    chain.fxReorder(5, 0);   // hall dropped onto overdrive's cell
    CHECK(layoutOf(chain) == expect({"hall", "overdrive", "delay"}));
  }
  SECTION("an empty (or out-of-range) target appends to the end") {
    chain.fxReorder(0, 7);   // slot 7 is empty
    CHECK(layoutOf(chain) == expect({"delay", "hall", "overdrive"}));
  }
  SECTION("empty source is a no-op and does NOT repack") {
    const Layout before = layoutOf(chain);
    chain.fxReorder(1, 0);   // slot 1 is empty
    chain.fxReorder(-1, 0);
    chain.fxReorder(Chain::kFxSlots, 0);
    CHECK(layoutOf(chain) == before);
  }
  SECTION("dropping a pedal onto itself keeps the order (but repacks)") {
    chain.fxReorder(2, 2);
    CHECK(layoutOf(chain) == expect({"overdrive", "delay", "hall"}));
  }
}

TEST_CASE("fxReorder forward/backward insertion lands just before the target",
          "[chain][grid]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  build(chain, f, {"overdrive", "chorus", "delay", "hall"});

  SECTION("forward: index shift after the erase is accounted for") {
    chain.fxReorder(1, 3);   // chorus dropped onto hall -> sits just before it
    CHECK(layoutOf(chain) == expect({"overdrive", "delay", "chorus", "hall"}));
  }
  SECTION("backward") {
    chain.fxReorder(3, 1);   // hall dropped onto chorus
    CHECK(layoutOf(chain) == expect({"overdrive", "hall", "chorus", "delay"}));
  }
}

TEST_CASE("fxMove swaps neighbors and stops at the edges", "[chain][grid]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  build(chain, f, {"overdrive", "delay"});

  chain.fxMove(0, +1);
  CHECK(layoutOf(chain) == expect({"delay", "overdrive"}));
  chain.fxMove(0, -1);                     // front edge: no-op
  CHECK(layoutOf(chain) == expect({"delay", "overdrive"}));
  chain.fxMove(Chain::kFxSlots - 1, +1);   // back edge: no-op
  CHECK(layoutOf(chain) == expect({"delay", "overdrive"}));
}

TEST_CASE("fxPlace replaces, fxRemove empties, held refs stay valid",
          "[chain][grid]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  build(chain, f, {"overdrive"});

  // Replace the occupant; the old instance disappears from lookups but a held
  // shared_ptr keeps it alive (stale but valid).
  std::shared_ptr<Effect> old = chain.fxAt(0);
  REQUIRE(old != nullptr);
  chain.fxPlace(0, f.create("fuzz"));
  CHECK(layoutOf(chain) == expect({"fuzz"}));
  CHECK(chain.find("overdrive") == nullptr);
  CHECK(chain.find("fuzz") != nullptr);
  CHECK(old->type_id() == "overdrive");   // still readable through our ref

  // Out-of-range and null placements are no-ops.
  chain.fxPlace(-1, f.create("delay"));
  chain.fxPlace(Chain::kFxSlots, f.create("delay"));
  chain.fxPlace(1, nullptr);
  CHECK(layoutOf(chain) == expect({"fuzz"}));

  // Remove: slot empties; removing an empty slot is a no-op.
  chain.fxRemove(0);
  chain.fxRemove(0);
  chain.fxRemove(-1);
  CHECK(layoutOf(chain) == expect({}));
  CHECK(chain.find("fuzz") == nullptr);
}

TEST_CASE("effects() lists prefix, occupied slots in order, then suffix",
          "[chain]") {
  Chain chain;
  FxFactory f;
  registerAllFx(f);
  chain.add(std::make_unique<fx::InputGain>())->section = Section::Input;
  chain.add(std::make_unique<fx::OutputGain>())->section = Section::Output;
  build(chain, f, {"overdrive", "", "delay"});

  auto ids = [&] {
    std::vector<std::string> v;
    for (const auto& fx : chain.effects()) v.push_back(fx->type_id());
    return v;
  };
  CHECK(ids() == std::vector<std::string>{"input", "overdrive", "delay", "output"});

  // A grid edit re-sorts the middle but never the fixed regions.
  chain.fxReorder(2, 0);
  CHECK(ids() == std::vector<std::string>{"input", "delay", "overdrive", "output"});

  // And the chain still processes cleanly across the gap-free relayout.
  std::vector<float> L((size_t)kBlock), R((size_t)kBlock);
  testutil::SignalGen gen;
  for (int b = 0; b < 20; b++) {
    gen.fill(L.data(), R.data(), kBlock);
    chain.process(L.data(), R.data(), kBlock);
    REQUIRE(testutil::firstBad(L.data(), kBlock, 100.0f) < 0);
    REQUIRE(testutil::firstBad(R.data(), kBlock, 100.0f) < 0);
  }
}

// test_rig_roundtrip.cpp -- rig + preset serialization round-trips.
//
// A rig is values-only (master/bpm/bypassed + fxGrid layout + per-effect
// enabled/footswitch/params); capture -> apply -> capture must be a fixed
// point, both in memory and through save/load on disk. Also covers the
// metadata identity rules (re-save/rename preserve id) and the store_util
// name guards that keep hostile web input out of the filesystem.

#include "chain.h"
#include "effects/comp.h"
#include "effects/eq.h"
#include "fx_factory.h"
#include "pedal_controls.h"
#include "presets.h"
#include "rigs.h"
#include "store_util.h"
#include "test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// Scratch dir per test case (ctest may run cases in parallel processes).
struct TestDir {
  fs::path root;
  explicit TestDir(const char* tag)
      : root(fs::temp_directory_path() / "pedalboard-tests" / tag) {
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~TestDir() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  std::string str() const { return root.string(); }
};

// A chain shaped like the app's: Input/Output singletons plus a grid with a
// gap (slot 1) and a duplicate kind (slot 3 becomes "delay-2") -- the two
// layout features a rig must reproduce exactly.
struct Rig {
  FxFactory f;
  Chain chain;
  PedalControls ctl;
  Rig(bool emptyGrid = false) {
    registerAllFx(f);
    chain.add(std::make_unique<fx::Comp>())->section = Section::Input;
    chain.add(std::make_unique<fx::EQ>())->section = Section::Output;
    if (!emptyGrid) {
      chain.fxPlaceInitial(0, f.create("overdrive"));
      chain.fxPlaceInitial(2, f.create("delay"));
      chain.fxPlaceInitial(3, f.create("delay"));  // -> "delay-2"
    }
    chain.finalize();
    chain.prepare(testutil::kRate, testutil::kBlock);
  }
};

// Deterministically randomize everything a rig captures.
void scramble(Rig& r, uint32_t seed) {
  testutil::Lcg rng(seed);
  for (const auto& fx : r.chain.effects()) {
    fx->enabled.store(rng.next() > 0.0f);
    fx->fsAssign.store((int)(std::fabs(rng.next()) * 4.9f) - 1);  // -1..3
    fx->fsMode.store(rng.next() > 0.0f ? 1 : 0);
    for (const auto& p : fx->params)
      p->set(p->min + (0.5f + 0.5f * rng.next()) * (p->max - p->min));
  }
  r.ctl.masterLevel.store(0.3f + std::fabs(rng.next()));
  r.ctl.bpm.store(20.0 + std::fabs(rng.next()) * 250.0);
  r.ctl.bypassed.store(rng.next() > 0.0f);
}

}  // namespace

TEST_CASE("rig capture -> apply -> capture is a fixed point", "[rigs]") {
  Rig a;
  scramble(a, 0xC0FFEE01u);
  const json doc = rigs::capture(a.chain, a.ctl);

  SECTION("onto a chain with the same layout") {
    Rig b;
    scramble(b, 0xDEADBEEFu);  // different state; apply must overwrite it all
    rigs::apply(doc, b.chain, b.ctl, b.f);
    CHECK(rigs::capture(b.chain, b.ctl) == doc);
  }
  SECTION("onto a chain with an EMPTY grid (loader rebuilds the slots)") {
    Rig b(/*emptyGrid=*/true);
    rigs::apply(doc, b.chain, b.ctl, b.f);
    CHECK(b.chain.fxGrid() == a.chain.fxGrid());  // gap + delay-2 recreated
    CHECK(rigs::capture(b.chain, b.ctl) == doc);
  }
}

TEST_CASE("rig save/load round-trips through disk", "[rigs][store]") {
  TestDir dir("rigs-roundtrip");
  Rig a;
  scramble(a, 0x12345678u);
  REQUIRE(rigs::save(dir.str(), "My Rig", a.chain, a.ctl));

  Rig b;
  scramble(b, 0x87654321u);
  REQUIRE(rigs::load(dir.str(), "My Rig", b.chain, b.ctl, b.f));
  CHECK(rigs::capture(b.chain, b.ctl) == rigs::capture(a.chain, a.ctl));

  CHECK(rigs::list(dir.str()) == std::vector<std::string>{"My Rig"});
  CHECK_FALSE(rigs::load(dir.str(), "No Such Rig", b.chain, b.ctl, b.f));
}

TEST_CASE("re-save and rename preserve a rig's identity", "[rigs][store]") {
  TestDir dir("rigs-identity");
  Rig a;

  std::string id1, id2;
  REQUIRE(rigs::save(dir.str(), "Sunday", a.chain, a.ctl, &id1));
  REQUIRE(!id1.empty());
  scramble(a, 0xFACEFEEDu);  // content changes; identity must not
  REQUIRE(rigs::save(dir.str(), "Sunday", a.chain, a.ctl, &id2));
  CHECK(id2 == id1);

  // Rename keeps the id (setlists reference rigs by id) and removes the old file.
  REQUIRE(rigs::rename(dir.str(), "Sunday", "Sunday AM"));
  CHECK(rigs::list(dir.str()) == std::vector<std::string>{"Sunday AM"});
  json doc;
  std::ifstream in(dir.root / "Sunday AM.json");
  REQUIRE(in.good());
  in >> doc;
  CHECK(doc.value("id", std::string{}) == id1);
  CHECK(doc.value("name", std::string{}) == "Sunday AM");

  // Renaming onto an existing rig must refuse rather than clobber it.
  REQUIRE(rigs::save(dir.str(), "Evening", a.chain, a.ctl));
  CHECK_FALSE(rigs::rename(dir.str(), "Evening", "Sunday AM"));
  CHECK_FALSE(rigs::rename(dir.str(), "No Such Rig", "Whatever"));
}

TEST_CASE("hostile names never touch the filesystem", "[rigs][store]") {
  TestDir dir("rigs-names");
  Rig a;

  const std::vector<std::string> bad = {
      "", ".", "..", ".hidden", "a/b", "a\\b", "..\\evil", "../evil",
      std::string("x\ny"), std::string(65, 'a')};
  for (const std::string& name : bad) {
    CAPTURE(name);
    CHECK_FALSE(store::validName(name));
    CHECK_FALSE(rigs::save(dir.str(), name, a.chain, a.ctl));
    CHECK_FALSE(rigs::load(dir.str(), name, a.chain, a.ctl, a.f));
    CHECK_FALSE(rigs::remove(dir.str(), name));
  }
  CHECK(rigs::list(dir.str()).empty());          // nothing was created
  CHECK(store::validName(std::string(64, 'a'))); // boundary: 64 chars is fine

  // A corrupt file loads as false and leaves the chain alone.
  { std::ofstream out(dir.root / "Broken.json"); out << "{not json"; }
  const json before = rigs::capture(a.chain, a.ctl);
  CHECK_FALSE(rigs::load(dir.str(), "Broken", a.chain, a.ctl, a.f));
  CHECK(rigs::capture(a.chain, a.ctl) == before);
}

TEST_CASE("preset save/load round-trips one pedal's knobs", "[presets][store]") {
  TestDir dir("presets-roundtrip");
  FxFactory f;
  registerAllFx(f);

  auto a = f.create("chorus");
  REQUIRE(a != nullptr);
  testutil::Lcg rng(0xBADC0DE5u);
  for (const auto& p : a->params)
    p->set(p->min + (0.5f + 0.5f * rng.next()) * (p->max - p->min));
  REQUIRE(presets::save(dir.str(), "chorus", "Warm", *a));

  auto b = f.create("chorus");
  b->enabled.store(false);
  REQUIRE(presets::load(dir.str(), "chorus", "Warm", *b));
  for (size_t i = 0; i < a->params.size(); i++) {
    CAPTURE(a->params[i]->id);
    CHECK(b->params[i]->get() == a->params[i]->get());
  }
  CHECK_FALSE(b->enabled.load());  // presets are knobs only, not enabled state

  CHECK(presets::list(dir.str(), "chorus") == std::vector<std::string>{"Warm"});
  CHECK_FALSE(presets::save(dir.str(), "../chorus", "Warm", *a));
  CHECK_FALSE(presets::load(dir.str(), "chorus", "../Warm", *b));
}

TEST_CASE("writeFileAtomic writes the payload and leaves no tmp behind",
          "[store]") {
  TestDir dir("store-atomic");
  const fs::path path = dir.root / "out.json";
  REQUIRE(store::writeFileAtomic(path, "{\"k\":1}"));
  std::ifstream in(path);
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  CHECK(data == "{\"k\":1}");
  CHECK_FALSE(fs::exists(dir.root / "out.json.tmp"));

  // Overwrite replaces the content whole.
  REQUIRE(store::writeFileAtomic(path, "{\"k\":2}"));
  std::ifstream in2(path);
  std::string data2((std::istreambuf_iterator<char>(in2)),
                    std::istreambuf_iterator<char>());
  CHECK(data2 == "{\"k\":2}");
}

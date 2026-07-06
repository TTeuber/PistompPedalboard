// test_registry.cpp -- factory registry + Param metadata sanity.
//
// These iterate the shared registry (fx_registry.cpp), so a newly added effect
// is covered automatically: unique kind keys, sane param ranges, clamping, and
// the duplicate-instance id suffixing the preset/web layers depend on.

#include "effect.h"
#include "fx_factory.h"
#include "fx_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

// Each test builds its own factory in place: create() mutates the per-kind
// instance counters, so tests about ids must not share one (and FxFactory owns
// a mutex, so it can't be copied out of a helper).

TEST_CASE("registry kinds are non-empty and unique", "[registry]") {
  FxFactory f;
  registerAllFx(f);
  REQUIRE(!f.kinds().empty());
  std::set<std::string> types;
  for (const FxKind& k : f.kinds()) {
    CAPTURE(k.type, k.name);
    CHECK(!k.type.empty());
    CHECK(!k.name.empty());
    CHECK(types.insert(k.type).second);  // no duplicate canonical keys
  }
}

TEST_CASE("every kind mints a working instance", "[registry]") {
  FxFactory f;
  registerAllFx(f);
  for (size_t i = 0; i < f.kinds().size(); i++) {
    auto fx = f.create(i);
    CAPTURE(f.kinds()[i].type);
    REQUIRE(fx != nullptr);
    CHECK(fx->type_id() == f.kinds()[i].type);  // first instance = canonical id
  }
}

TEST_CASE("param metadata is sane for every effect", "[registry][params]") {
  FxFactory f;
  registerAllFx(f);
  for (const FxKind& k : f.kinds()) {
    auto fx = k.make();
    REQUIRE(fx != nullptr);
    std::set<std::string> ids;
    for (const auto& p : fx->params) {
      CAPTURE(k.type, p->id);
      CHECK(!p->id.empty());
      CHECK(!p->name.empty());
      CHECK(p->min < p->max);
      CHECK(p->def >= p->min);
      CHECK(p->def <= p->max);
      CHECK(p->get() == p->def);           // fresh instance starts at default
      CHECK(ids.insert(p->id).second);     // ids unique within the effect
    }
  }
}

TEST_CASE("Param::set clamps to [min,max]", "[params]") {
  FxFactory f;
  registerAllFx(f);
  for (const FxKind& k : f.kinds()) {
    auto fx = k.make();
    REQUIRE(fx != nullptr);
    for (const auto& p : fx->params) {
      CAPTURE(k.type, p->id);
      p->set(p->max + 1000.0f);
      CHECK(p->get() == p->max);
      p->set(p->min - 1000.0f);
      CHECK(p->get() == p->min);
      const float mid = p->min + (p->max - p->min) * 0.5f;
      p->set(mid);
      CHECK(p->get() == mid);
    }
  }
}

TEST_CASE("duplicate instances get unique suffixed ids", "[registry][factory]") {
  FxFactory f;
  registerAllFx(f);
  auto a = f.create("delay");
  auto b = f.create("delay");
  auto c = f.create("delay");
  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  REQUIRE(c != nullptr);
  CHECK(a->type_id() == "delay");
  CHECK(b->type_id() == "delay-2");
  CHECK(c->type_id() == "delay-3");
}

TEST_CASE("createRestored bumps the counter past restored ids", "[factory]") {
  FxFactory f;
  registerAllFx(f);
  // Restore a saved "reverse-3": a later picker-create must not collide.
  auto restored = f.createRestored("reverse", "reverse-3");
  REQUIRE(restored != nullptr);
  CHECK(restored->type_id() == "reverse-3");
  auto next = f.create("reverse");
  REQUIRE(next != nullptr);
  CHECK(next->type_id() == "reverse-4");
}

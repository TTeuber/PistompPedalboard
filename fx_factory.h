// fx_factory.h -- builds fresh FX instances for the runtime-reorderable grid.
//
// The FX grid lets the user place effects into 8 slots, including MORE THAN ONE
// of the same kind. That means effects can no longer be pre-instantiated
// singletons: the picker has to mint a new instance on demand. This registry is
// the single place that knows how to do that.
//
// Each kind registers a no-arg maker (a fresh, unprepared Effect). create()
// builds one and assigns it a unique type_id -- the canonical id for the first
// instance of a kind ("reverb") and a suffixed id for each subsequent one
// ("reverb-2", "reverb-3", ...) -- because type_id is the preset/web/find key
// and must stay unique within the chain.

#pragma once

#include "effect.h"

#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct FxKind {
  std::string type;   // canonical key, e.g. "reverb"
  std::string name;   // display label, e.g. "Reverb"
  std::function<std::unique_ptr<Effect>()> make;
};

class FxFactory {
public:
  void add(std::string type, std::string name,
           std::function<std::unique_ptr<Effect>()> make) {
    kinds_.push_back({std::move(type), std::move(name), std::move(make)});
  }

  const std::vector<FxKind>& kinds() const { return kinds_; }

  // Build a fresh (unprepared) instance of kinds_[i] with a unique type_id.
  // Safe to call from the UI and web threads concurrently (counts_ is guarded).
  std::unique_ptr<Effect> create(size_t i) {
    if (i >= kinds_.size()) return nullptr;
    const FxKind& k = kinds_[i];
    auto fx = k.make();
    if (!fx) return nullptr;
    std::lock_guard<std::mutex> lk(countsMutex_);
    int n = ++counts_[k.type];
    if (n > 1) fx->set_type_id(k.type + "-" + std::to_string(n));
    return fx;
  }

  // Convenience: build a fresh instance of the kind with this canonical type
  // (e.g. "reverb"). Returns nullptr if no such kind is registered. Used to seed
  // the default chain by name rather than by fragile registration index.
  std::unique_ptr<Effect> create(const std::string& type) {
    for (size_t i = 0; i < kinds_.size(); i++)
      if (kinds_[i].type == type) return create(i);
    return nullptr;
  }

  // Rebuild a saved instance: mint the named kind but force its stored type_id
  // (so a preset's params/footswitch state reattach by id). Bumps the per-kind
  // counter past this id's suffix so later picker-created instances can't
  // collide with restored ones. Used by the preset loader.
  std::unique_ptr<Effect> createRestored(const std::string& kind,
                                         const std::string& id) {
    for (const FxKind& k : kinds_) {
      if (k.type != kind) continue;
      auto fx = k.make();
      if (!fx) return nullptr;
      if (!id.empty()) fx->set_type_id(id);
      std::lock_guard<std::mutex> lk(countsMutex_);
      int& c = counts_[kind];
      int n = idSuffix(id);
      if (n > c) c = n;
      return fx;
    }
    return nullptr;
  }

private:
  // Trailing instance number of a factory id: "reverb" -> 1, "reverb-2" -> 2.
  static int idSuffix(const std::string& id) {
    auto dash = id.rfind('-');
    if (dash == std::string::npos || dash + 1 >= id.size()) return 1;
    const std::string tail = id.substr(dash + 1);
    for (char ch : tail)
      if (!std::isdigit((unsigned char)ch)) return 1;
    return std::stoi(tail);
  }

  std::vector<FxKind> kinds_;
  std::map<std::string, int> counts_;   // per-kind instance count, for unique ids
  std::mutex countsMutex_;
};

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

private:
  std::vector<FxKind> kinds_;
  std::map<std::string, int> counts_;   // per-kind instance count, for unique ids
  std::mutex countsMutex_;
};

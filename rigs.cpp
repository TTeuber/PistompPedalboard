// rigs.cpp -- see rigs.h.

#include "rigs.h"

#include "chain.h"
#include "pedal_controls.h"
#include "effect.h"
#include "fx_factory.h"
#include "fx_id.h"
#include "meta.h"
#include "store_util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace rigs {

nlohmann::json capture(const Chain& chain, const PedalControls& ctl) {
  json doc;
  doc["master"] = ctl.masterLevel.load();
  doc["bpm"] = ctl.bpm.load();
  doc["bypassed"] = ctl.bypassed.load();

  // FX grid layout: per slot, which kind sits there (null = empty). The loader
  // recreates instances from this; `id` keys the effects map below.
  json grid = json::array();
  auto slots = chain.fxGrid();
  for (int i = 0; i < Chain::fxSlotCount(); i++) {
    if (slots[(size_t)i].empty()) { grid.push_back(nullptr); continue; }
    grid.push_back(json{{"kind", fxBaseKind(slots[(size_t)i])},
                        {"id", slots[(size_t)i]}});
  }
  doc["fxGrid"] = grid;

  json effects = json::object();
  for (const auto& fx : chain.effects()) {
    json e;
    e["enabled"] = fx->enabled.load();
    e["fsAssign"] = fx->fsAssign.load();   // footswitch binding (phase 3)
    e["fsMode"] = fx->fsMode.load();
    json params = json::object();
    for (const auto& p : fx->params) params[p->id] = p->get();
    e["params"] = params;
    effects[fx->type_id()] = e;
  }
  doc["effects"] = effects;
  return doc;
}

void apply(const nlohmann::json& doc, Chain& chain, PedalControls& ctl,
           FxFactory& factory) {
  if (doc.contains("master") && doc["master"].is_number())
    ctl.masterLevel.store((float)doc["master"].get<double>());
  if (doc.contains("bpm") && doc["bpm"].is_number())
    ctl.bpm.store(std::clamp(doc["bpm"].get<double>(), 20.0, 300.0));
  if (doc.contains("bypassed") && doc["bypassed"].is_boolean())
    ctl.bypassed.store(doc["bypassed"].get<bool>());

  // Rebuild the FX grid first, so the effects loop below finds the restored
  // instances by id. Rigs saved before this field simply leave the grid as
  // is (older behaviour: values-only over the current layout).
  if (doc.contains("fxGrid") && doc["fxGrid"].is_array()) {
    const json& grid = doc["fxGrid"];
    for (int slot = 0;
         slot < Chain::fxSlotCount() && slot < (int)grid.size(); slot++) {
      const json& s = grid[(size_t)slot];
      if (!s.is_object()) { chain.fxRemove(slot); continue; }  // null = empty
      std::string id = s.value("id", std::string{});
      std::string kind = s.value("kind", std::string{});
      if (kind.empty()) kind = fxBaseKind(id);
      // Leave the slot alone if it already holds the right instance (avoids
      // tearing down the default grid when loading a matching preset).
      std::shared_ptr<Effect> cur = chain.fxAt(slot);
      if (cur && cur->type_id() == id) continue;
      if (auto fx = factory.createRestored(kind, id))
        chain.fxPlace(slot, std::move(fx));
      else
        chain.fxRemove(slot);
    }
  }

  if (!doc.contains("effects") || !doc["effects"].is_object()) return;
  for (auto it = doc["effects"].begin(); it != doc["effects"].end(); ++it) {
    std::shared_ptr<Effect> fx = chain.find(it.key());
    if (!fx) continue;
    const json& e = it.value();
    if (e.contains("enabled") && e["enabled"].is_boolean())
      fx->enabled.store(e["enabled"].get<bool>());
    if (e.contains("fsAssign") && e["fsAssign"].is_number_integer())
      fx->fsAssign.store(e["fsAssign"].get<int>());
    if (e.contains("fsMode") && e["fsMode"].is_number_integer())
      fx->fsMode.store(e["fsMode"].get<int>());
    if (e.contains("params") && e["params"].is_object()) {
      for (auto pit = e["params"].begin(); pit != e["params"].end(); ++pit) {
        if (Param* p = fx->param(pit.key()))
          if (pit.value().is_number()) p->set((float)pit.value().get<double>());
      }
    }
  }
}

std::vector<std::string> list(const std::string& dir) {
  std::vector<std::string> names;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return names;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".json")
      names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool load(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl, FxFactory& factory) {
  if (!store::validName(name)) return false;
  std::ifstream in(fs::path(dir) / (name + ".json"));
  if (!in) return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception&) {
    return false;
  }
  apply(doc, chain, ctl, factory);
  return true;
}

bool save(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl, std::string* idOut) {
  if (!store::validName(name)) return false;
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::path path = fs::path(dir) / (name + ".json");

  // Content (master/bypassed/fxGrid/effects) is what we hash; the metadata
  // wraps it. Preserve identity (id/createdAt/provenance) from any existing file
  // so re-saving a rig updates it in place rather than minting a new identity.
  json content = capture(chain, ctl);
  json doc = content;
  json prev;
  { std::ifstream in(path); if (in) { try { in >> prev; } catch (...) {} } }
  if (prev.is_object()) {
    for (const char* k : {"id", "createdAt", "origin", "owner"})
      if (prev.contains(k)) doc[k] = prev[k];
  }
  doc["name"] = name;
  meta::stamp(doc, content, 2);
  if (idOut) *idOut = doc.value("id", std::string{});

  return store::writeFileAtomic(path, doc.dump(2));
}

bool remove(const std::string& dir, const std::string& name) {
  if (!store::validName(name)) return false;
  std::error_code ec;
  return fs::remove(fs::path(dir) / (name + ".json"), ec);
}

bool rename(const std::string& dir, const std::string& from,
            const std::string& to) {
  if (!store::validName(from) || !store::validName(to)) return false;
  if (from == to) return true;
  fs::path src = fs::path(dir) / (from + ".json");
  fs::path dst = fs::path(dir) / (to + ".json");
  std::error_code ec;
  if (!fs::exists(src, ec)) return false;
  if (fs::exists(dst, ec)) return false;  // don't clobber a different rig

  // Re-write under the new name but keep the existing id/createdAt/provenance,
  // so setlists keyed on this rig's id still link to it after the rename.
  json doc;
  { std::ifstream in(src); if (!in) return false;
    try { in >> doc; } catch (...) { return false; } }
  doc["name"] = to;
  if (!store::writeFileAtomic(dst, doc.dump(2))) return false;
  fs::remove(src, ec);
  return true;
}

}  // namespace rigs

// presets.cpp -- see presets.h.

#include "presets.h"

#include "chain.h"
#include "pedal_controls.h"
#include "effect.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace presets {

nlohmann::json capture(const Chain& chain, const PedalControls& ctl) {
  json doc;
  doc["master"] = ctl.masterLevel.load();
  doc["bypassed"] = ctl.bypassed.load();
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

void apply(const nlohmann::json& doc, Chain& chain, PedalControls& ctl) {
  if (doc.contains("master") && doc["master"].is_number())
    ctl.masterLevel.store((float)doc["master"].get<double>());
  if (doc.contains("bypassed") && doc["bypassed"].is_boolean())
    ctl.bypassed.store(doc["bypassed"].get<bool>());

  if (!doc.contains("effects") || !doc["effects"].is_object()) return;
  for (auto it = doc["effects"].begin(); it != doc["effects"].end(); ++it) {
    Effect* fx = chain.find(it.key());
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
          PedalControls& ctl) {
  std::ifstream in(fs::path(dir) / (name + ".json"));
  if (!in) return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception&) {
    return false;
  }
  apply(doc, chain, ctl);
  return true;
}

bool save(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  json doc = capture(chain, ctl);
  doc["name"] = name;
  std::ofstream out(fs::path(dir) / (name + ".json"));
  if (!out) return false;
  out << doc.dump(2);
  return true;
}

}  // namespace presets

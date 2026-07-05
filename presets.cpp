// presets.cpp -- see presets.h.

#include "presets.h"

#include "effect.h"
#include "meta.h"
#include "store_util.h"

#include <json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace presets {

std::vector<std::string> list(const std::string& dir, const std::string& kind) {
  std::vector<std::string> names;
  if (!store::validName(kind)) return names;
  std::error_code ec;
  fs::path d = fs::path(dir) / kind;
  if (!fs::is_directory(d, ec)) return names;
  for (const auto& entry : fs::directory_iterator(d, ec)) {
    if (entry.path().extension() == ".json")
      names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool load(const std::string& dir, const std::string& kind,
          const std::string& name, Effect& fx) {
  if (!store::validName(kind) || !store::validName(name)) return false;
  std::ifstream in(fs::path(dir) / kind / (name + ".json"));
  if (!in) return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception&) {
    return false;
  }
  // Values only: set each matching param (clamped); leave enabled/footswitch be.
  if (doc.contains("params") && doc["params"].is_object()) {
    for (auto it = doc["params"].begin(); it != doc["params"].end(); ++it) {
      if (Param* p = fx.param(it.key()))
        if (it.value().is_number()) p->set((float)it.value().get<double>());
    }
  }
  return true;
}

bool save(const std::string& dir, const std::string& kind,
          const std::string& name, const Effect& fx) {
  if (!store::validName(kind) || !store::validName(name)) return false;
  std::error_code ec;
  fs::path d = fs::path(dir) / kind;
  fs::create_directories(d, ec);
  fs::path path = d / (name + ".json");

  // Content is kind + param values; metadata wraps it. Preserve identity across
  // saves so re-saving a preset updates it in place.
  json content;
  content["kind"] = kind;
  json params = json::object();
  for (const auto& p : fx.params) params[p->id] = p->get();
  content["params"] = params;

  json doc = content;
  json prev;
  { std::ifstream in(path); if (in) { try { in >> prev; } catch (...) {} } }
  if (prev.is_object()) {
    for (const char* k : {"id", "createdAt", "origin", "owner"})
      if (prev.contains(k)) doc[k] = prev[k];
  }
  doc["name"] = name;
  meta::stamp(doc, content, 2);

  return store::writeFileAtomic(path, doc.dump(2));
}

bool remove(const std::string& dir, const std::string& kind,
            const std::string& name) {
  if (!store::validName(kind) || !store::validName(name)) return false;
  std::error_code ec;
  return fs::remove(fs::path(dir) / kind / (name + ".json"), ec);
}

bool rename(const std::string& dir, const std::string& kind,
            const std::string& from, const std::string& to) {
  if (!store::validName(kind) || !store::validName(from) ||
      !store::validName(to))
    return false;
  if (from == to) return true;
  fs::path d = fs::path(dir) / kind;
  fs::path src = d / (from + ".json");
  fs::path dst = d / (to + ".json");
  std::error_code ec;
  if (!fs::exists(src, ec)) return false;
  if (fs::exists(dst, ec)) return false;  // don't clobber a different preset

  // Re-write under the new name, keeping id/createdAt/provenance from the file.
  json doc;
  { std::ifstream in(src); if (!in) return false;
    try { in >> doc; } catch (...) { return false; } }
  doc["name"] = to;
  if (!store::writeFileAtomic(dst, doc.dump(2))) return false;
  fs::remove(src, ec);
  return true;
}

}  // namespace presets

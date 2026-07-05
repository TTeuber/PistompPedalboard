// setlists.cpp -- see setlists.h.

#include "setlists.h"

#include "meta.h"
#include "store_util.h"

#include <json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace setlists {

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

bool load(const std::string& dir, const std::string& name,
          std::vector<RigRef>& rigsOut, std::string* idOut) {
  if (!store::validName(name)) return false;
  std::ifstream in(fs::path(dir) / (name + ".json"));
  if (!in) return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception&) {
    return false;
  }
  if (idOut) *idOut = doc.value("id", std::string{});
  rigsOut.clear();
  if (doc.contains("rigs") && doc["rigs"].is_array()) {
    for (const auto& r : doc["rigs"]) {
      if (r.is_string())
        rigsOut.push_back({"", r.get<std::string>()});  // legacy bare name
      else if (r.is_object())
        rigsOut.push_back({r.value("id", std::string{}),
                           r.value("name", std::string{})});
    }
  }
  return true;
}

bool save(const std::string& dir, const std::string& name,
          const std::vector<RigRef>& rigs, std::string* idOut) {
  if (!store::validName(name)) return false;
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::path path = fs::path(dir) / (name + ".json");

  json content;
  json refs = json::array();
  for (const auto& r : rigs) refs.push_back(json{{"id", r.id}, {"name", r.name}});
  content["rigs"] = refs;

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

}  // namespace setlists

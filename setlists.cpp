// setlists.cpp -- see setlists.h.

#include "setlists.h"

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
          std::vector<std::string>& rigsOut) {
  std::ifstream in(fs::path(dir) / (name + ".json"));
  if (!in) return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception&) {
    return false;
  }
  rigsOut.clear();
  if (doc.contains("rigs") && doc["rigs"].is_array()) {
    for (const auto& r : doc["rigs"])
      if (r.is_string()) rigsOut.push_back(r.get<std::string>());
  }
  return true;
}

bool save(const std::string& dir, const std::string& name,
          const std::vector<std::string>& rigs) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  json doc;
  doc["name"] = name;
  doc["rigs"] = rigs;
  std::ofstream out(fs::path(dir) / (name + ".json"));
  if (!out) return false;
  out << doc.dump(2);
  return true;
}

bool remove(const std::string& dir, const std::string& name) {
  std::error_code ec;
  return fs::remove(fs::path(dir) / (name + ".json"), ec);
}

}  // namespace setlists

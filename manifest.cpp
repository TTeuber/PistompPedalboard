// manifest.cpp -- see manifest.h.

#include "manifest.h"

#include "meta.h"
#include "store_util.h"

#include <json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using nlohmann::json;

namespace manifest {

namespace {

constexpr int kItemSchema = 2;     // rig/setlist/preset document version
constexpr int kManifestSchema = 1;  // manifest.json version

bool readJson(const fs::path& p, json& out) {
  std::ifstream in(p);
  if (!in) return false;
  try {
    in >> out;
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

bool writeJson(const fs::path& p, const json& doc) {
  return store::writeFileAtomic(p, doc.dump(2));
}

// Path relative to the library base, forward slashes (stable across platforms).
std::string relPath(const fs::path& base, const fs::path& p) {
  std::error_code ec;
  fs::path rel = fs::relative(p, base, ec);
  return ec ? p.filename().string() : rel.generic_string();
}

bool hasId(const json& doc) {
  return doc.contains("id") && doc["id"].is_string() &&
         !doc["id"].get<std::string>().empty();
}

// The value-only subset hashed for `contentHash` -- never the metadata fields.
json contentSubset(const std::string& type, const json& doc) {
  json c = json::object();
  if (type == "rig") {
    for (const char* k : {"master", "bypassed", "fxGrid", "effects"})
      if (doc.contains(k)) c[k] = doc[k];
  } else if (type == "preset") {
    for (const char* k : {"kind", "params"})
      if (doc.contains(k)) c[k] = doc[k];
  } else {  // setlist
    if (doc.contains("rigs")) c["rigs"] = doc["rigs"];
  }
  return c;
}

// A file's mtime as ISO-8601 UTC, falling back to now if it can't be read.
std::string mtimeIso(const fs::path& p) {
  std::error_code ec;
  auto ft = fs::last_write_time(p, ec);
  if (ec) return meta::nowIso8601();
  // Pre-C++20 file_clock -> system_clock conversion (the standard shim).
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  std::time_t t = std::chrono::system_clock::to_time_t(sctp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// Stamp a freshly-discovered (id-less) file, seeding timestamps from its mtime
// so an existing library keeps its real ages instead of all reading "now".
void stampNew(json& doc, const std::string& type, const std::string& ts) {
  doc["schema"] = kItemSchema;
  doc["id"] = meta::uuidv4();
  doc["createdAt"] = ts;
  doc["updatedAt"] = ts;
  doc["contentHash"] = meta::contentHash(contentSubset(type, doc));
  doc["origin"] = json{{"by", nullptr}, {"sourceId", nullptr}};
  doc["owner"] = nullptr;
}

Item itemOf(const std::string& type, const json& doc,
            const std::string& rel) {
  Item it;
  it.id = doc.value("id", std::string{});
  it.type = type;
  it.name = doc.value("name", std::string{});
  it.kind = doc.value("kind", std::string{});
  it.path = rel;
  it.updatedAt = doc.value("updatedAt", std::string{});
  it.hash = doc.value("contentHash", std::string{});
  return it;
}

void writeManifest(const fs::path& base, const std::vector<Item>& items) {
  json doc;
  doc["schema"] = kManifestSchema;
  doc["generatedAt"] = meta::nowIso8601();
  json arr = json::array();
  for (const auto& it : items) {
    json j = {{"id", it.id},     {"type", it.type},
              {"name", it.name}, {"path", it.path},
              {"updatedAt", it.updatedAt}, {"hash", it.hash}};
    if (!it.kind.empty()) j["kind"] = it.kind;
    arr.push_back(j);
  }
  doc["items"] = arr;
  writeJson(base / "manifest.json", doc);
}

}  // namespace

std::string Index::idForName(const std::string& type,
                             const std::string& name) const {
  for (const auto& it : items)
    if (it.type == type && it.name == name) return it.id;
  return "";
}

std::string Index::nameForId(const std::string& id) const {
  if (id.empty()) return "";
  for (const auto& it : items)
    if (it.id == id) return it.name;
  return "";
}

Index load(const std::string& base) {
  Index idx;
  json doc;
  if (!readJson(fs::path(base) / "manifest.json", doc)) return idx;
  if (doc.contains("items") && doc["items"].is_array()) {
    for (const auto& j : doc["items"]) {
      Item it;
      it.id = j.value("id", std::string{});
      it.type = j.value("type", std::string{});
      it.name = j.value("name", std::string{});
      it.kind = j.value("kind", std::string{});
      it.path = j.value("path", std::string{});
      it.updatedAt = j.value("updatedAt", std::string{});
      it.hash = j.value("hash", std::string{});
      idx.items.push_back(it);
    }
  }
  return idx;
}

void rebuild(const std::string& base) {
  fs::path b(base);
  std::vector<Item> items;
  std::map<std::string, std::string> rigNameToId;
  std::error_code ec;

  // 1) Rigs -- stamp if needed, and build a name->id map for setlist refs.
  fs::path rigDir = b / "rigs";
  if (fs::is_directory(rigDir, ec)) {
    for (const auto& e : fs::directory_iterator(rigDir, ec)) {
      if (e.path().extension() != ".json") continue;
      json doc;
      if (!readJson(e.path(), doc)) continue;
      if (!hasId(doc)) {
        if (!doc.contains("name")) doc["name"] = e.path().stem().string();
        stampNew(doc, "rig", mtimeIso(e.path()));
        writeJson(e.path(), doc);
      }
      Item it = itemOf("rig", doc, relPath(b, e.path()));
      rigNameToId[doc.value("name", e.path().stem().string())] = it.id;
      items.push_back(it);
    }
  }

  // 2) Presets -- nested one level under presets/<kind>/.
  fs::path presetDir = b / "presets";
  if (fs::is_directory(presetDir, ec)) {
    for (const auto& kindEnt : fs::directory_iterator(presetDir, ec)) {
      if (!kindEnt.is_directory()) continue;
      for (const auto& e : fs::directory_iterator(kindEnt.path(), ec)) {
        if (e.path().extension() != ".json") continue;
        json doc;
        if (!readJson(e.path(), doc)) continue;
        if (!hasId(doc)) {
          if (!doc.contains("name")) doc["name"] = e.path().stem().string();
          if (!doc.contains("kind"))
            doc["kind"] = kindEnt.path().filename().string();
          stampNew(doc, "preset", mtimeIso(e.path()));
          writeJson(e.path(), doc);
        }
        items.push_back(itemOf("preset", doc, relPath(b, e.path())));
      }
    }
  }

  // 3) Setlists -- stamp + migrate legacy "name" refs to {id, name} objects.
  fs::path setlistDir = b / "setlists";
  if (fs::is_directory(setlistDir, ec)) {
    for (const auto& e : fs::directory_iterator(setlistDir, ec)) {
      if (e.path().extension() != ".json") continue;
      json doc;
      if (!readJson(e.path(), doc)) continue;
      bool changed = false;
      if (!doc.contains("name")) {
        doc["name"] = e.path().stem().string();
        changed = true;
      }
      if (doc.contains("rigs") && doc["rigs"].is_array()) {
        json out = json::array();
        for (const auto& r : doc["rigs"]) {
          if (r.is_string()) {
            std::string nm = r.get<std::string>();
            auto hit = rigNameToId.find(nm);
            out.push_back(json{{"id", hit != rigNameToId.end() ? hit->second : ""},
                               {"name", nm}});
            changed = true;
          } else if (r.is_object()) {
            out.push_back(r);
          }
        }
        doc["rigs"] = out;
      }
      if (!hasId(doc)) {
        stampNew(doc, "setlist", mtimeIso(e.path()));
        changed = true;
      } else if (changed) {
        // Refs migrated on an already-stamped file: refresh hash + updatedAt.
        doc["contentHash"] = meta::contentHash(contentSubset("setlist", doc));
        doc["updatedAt"] = meta::nowIso8601();
      }
      if (changed) writeJson(e.path(), doc);
      items.push_back(itemOf("setlist", doc, relPath(b, e.path())));
    }
  }

  writeManifest(b, items);
}

void upsertFile(const std::string& base, const std::string& type,
                const std::string& absFile) {
  fs::path b(base);
  fs::path p(absFile);
  json doc;
  if (!readJson(p, doc)) return;
  Item ni = itemOf(type, doc, relPath(b, p));

  Index idx = load(base);
  bool replaced = false;
  for (auto& it : idx.items) {
    if ((!ni.id.empty() && it.id == ni.id) || it.path == ni.path) {
      it = ni;
      replaced = true;
      break;
    }
  }
  if (!replaced) idx.items.push_back(ni);
  writeManifest(b, idx.items);
}

void removeFile(const std::string& base, const std::string& absFile) {
  fs::path b(base);
  std::string rel = relPath(b, fs::path(absFile));
  Index idx = load(base);
  std::vector<Item> kept;
  for (auto& it : idx.items)
    if (it.path != rel) kept.push_back(it);
  writeManifest(b, kept);
}

}  // namespace manifest

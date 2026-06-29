// manifest.h -- the library index over rigs/setlists/presets.
//
// `manifest.json` (at the library root, sibling of rigs/ setlists/ presets/) is
// a DERIVED cache listing every item's {id, type, name, path, updatedAt, hash}.
// The JSON files on disk are the source of truth; the manifest can be deleted
// and regenerated from them at any time (`rebuild`), which is also what makes an
// out-of-band edit survivable. It exists so a future sync layer can diff two
// libraries by id+hash without opening every file, and so setlists can resolve
// their rig references (stored by id) back to a current name. See ADR 0002.

#pragma once

#include <string>
#include <vector>

namespace manifest {

struct Item {
  std::string id;
  std::string type;  // "rig" | "setlist" | "preset"
  std::string name;
  std::string kind;  // presets only (the effect kind); empty otherwise
  std::string path;  // relative to the library base, forward slashes
  std::string updatedAt;
  std::string hash;
};

struct Index {
  std::vector<Item> items;
  // The id of the item of `type` whose name matches, or "" if none.
  std::string idForName(const std::string& type, const std::string& name) const;
  // The current name of the item with this id, or "" if the id is unknown.
  std::string nameForId(const std::string& id) const;
};

// Read manifest.json under `base`. Returns an empty index if it's absent/bad.
Index load(const std::string& base);

// Full scan + one-time migration: stamp any rig/setlist/preset file that lacks
// an id (createdAt/updatedAt seeded from the file mtime), rewrite legacy setlist
// rig references (name string -> {id, name}), then (re)write manifest.json.
// Idempotent: already-stamped files are left untouched on subsequent runs.
void rebuild(const std::string& base);

// Insert/replace the manifest entry for a just-saved file (read back to pick up
// its stamped id/updatedAt/hash). `type` is "rig" | "setlist" | "preset".
void upsertFile(const std::string& base, const std::string& type,
                const std::string& absFile);

// Drop the manifest entry for a just-deleted file (matched by path).
void removeFile(const std::string& base, const std::string& absFile);

}  // namespace manifest

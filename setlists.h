// setlists.h -- save/load "setlists": an ordered list of rig references.
//
// A setlist is just a playlist of rigs (rigs.h) in the order you'll step
// through them on stage. The web UI owns the "active setlist + index" and steps
// by loading each rig in turn; this module only persists the ordering.
//
// Each entry references a rig by its stable `id` (rename-safe) with `name` kept
// as a cached label, so a renamed rig still resolves and a deleted one can still
// render gracefully. Legacy files that stored bare name strings still load.
//
// Layout on disk: <dir>/<name>.json =
//   { ...metadata, rigs:[ { "id":"...", "name":"Clean Worship" }, ... ] }

#pragma once

#include <string>
#include <vector>

namespace setlists {

// A rig reference: `id` is the canonical link, `name` a cached display label.
struct RigRef {
  std::string id;
  std::string name;
};

// List setlist names (filenames without .json) found in `dir`.
std::vector<std::string> list(const std::string& dir);

// Read <dir>/<name>.json into `rigsOut` (the ordered rig refs). Tolerates both
// legacy bare-string entries (id left empty) and {id,name} objects. `idOut`, if
// given, receives the setlist's own stable id. false on read/parse error.
bool load(const std::string& dir, const std::string& name,
          std::vector<RigRef>& rigsOut, std::string* idOut = nullptr);

// Write the ordered rig refs to <dir>/<name>.json. Preserves the file's metadata
// identity across saves. false on write error. `idOut` receives the setlist id.
bool save(const std::string& dir, const std::string& name,
          const std::vector<RigRef>& rigs, std::string* idOut = nullptr);

// Delete <dir>/<name>.json. false if it didn't exist / couldn't remove.
bool remove(const std::string& dir, const std::string& name);

}  // namespace setlists

// setlists.h -- save/load "setlists": an ordered list of rig names.
//
// A setlist is just a playlist of rigs (rigs.h) in the order you'll step
// through them on stage. The web UI owns the "active setlist + index" and steps
// by loading each rig in turn; this module only persists the ordering.
//
// Layout on disk: <dir>/<name>.json = { name, rigs:[ "Clean Worship", ... ] }

#pragma once

#include <string>
#include <vector>

namespace setlists {

// List setlist names (filenames without .json) found in `dir`.
std::vector<std::string> list(const std::string& dir);

// Read <dir>/<name>.json into `rigsOut` (the ordered rig names). false on
// read/parse error.
bool load(const std::string& dir, const std::string& name,
          std::vector<std::string>& rigsOut);

// Write the ordered rig names to <dir>/<name>.json. false on write error.
bool save(const std::string& dir, const std::string& name,
          const std::vector<std::string>& rigs);

// Delete <dir>/<name>.json. false if it didn't exist / couldn't remove.
bool remove(const std::string& dir, const std::string& name);

}  // namespace setlists

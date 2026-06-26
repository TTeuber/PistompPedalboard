// presets.h -- save/load per-pedal "presets" as JSON files on the Pi.
//
// A preset is a knob snapshot for a single effect KIND (e.g. a Drive preset
// "Light Crunch" / "Hard Overdrive"). It captures param VALUES ONLY -- not the
// enabled flag, not the footswitch binding -- so loading one re-voices a pedal
// without touching how it sits in the chain. Presets are scoped by kind, so a
// preset saved on one Drive applies to any Drive instance (drive, drive-2, ...).
//
// Layout on disk: <dir>/<kind>/<name>.json = { name, kind, params:{ id:value } }
//
// (The whole-chain snapshot is a separate, larger concept; see rigs.h.)

#pragma once

#include <string>
#include <vector>

class Effect;

namespace presets {

// List preset names (filenames without .json) found in <dir>/<kind>/.
std::vector<std::string> list(const std::string& dir, const std::string& kind);

// Load <dir>/<kind>/<name>.json onto `fx`, setting each matching param (clamped
// to range). Unknown params are ignored; enabled/footswitch are left untouched.
// false on read/parse error.
bool load(const std::string& dir, const std::string& kind,
          const std::string& name, Effect& fx);

// Snapshot fx's current param values to <dir>/<kind>/<name>.json. false on
// write error.
bool save(const std::string& dir, const std::string& kind,
          const std::string& name, const Effect& fx);

// Delete <dir>/<kind>/<name>.json. false if it didn't exist / couldn't remove.
bool remove(const std::string& dir, const std::string& kind,
            const std::string& name);

}  // namespace presets

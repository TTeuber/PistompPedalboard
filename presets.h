// presets.h -- save/load pedalboard patches as JSON files on the Pi.
//
// A preset captures the values-only state of the chain: per-effect enabled flag +
// each param value, plus the global master/bypass. The same capture()/apply()
// helpers back the web layer's preset endpoints, so there's one mapping between
// the live chain and JSON.

#pragma once

#include <json.hpp>

#include <string>
#include <vector>

class Chain;
struct PedalControls;

namespace presets {

// List preset names (filenames without .json) found in `dir`.
std::vector<std::string> list(const std::string& dir);

// Load dir/<name>.json into the chain. false on read/parse error.
bool load(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl);

// Snapshot the chain to dir/<name>.json. false on write error.
bool save(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl);

// Values-only document: { name, master, bypassed, effects:{type:{enabled,params}} }.
nlohmann::json capture(const Chain& chain, const PedalControls& ctl);

// Apply a values-only document; missing keys leave current values untouched.
void apply(const nlohmann::json& doc, Chain& chain, PedalControls& ctl);

}  // namespace presets

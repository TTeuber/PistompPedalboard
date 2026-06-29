// rigs.h -- save/load whole-chain "rigs" as JSON files on the Pi.
//
// A rig captures the values-only state of the entire chain: per-effect enabled
// flag + each param value, plus the global master/bypass and the FX grid
// layout. The same capture()/apply() helpers back the web layer's rig
// endpoints, so there's one mapping between the live chain and JSON.
//
// (A per-pedal "preset" -- a knob snapshot for a single effect kind -- is a
// separate, smaller concept; see presets.h.)

#pragma once

#include <json.hpp>

#include <string>
#include <vector>

class Chain;
class FxFactory;
struct PedalControls;

namespace rigs {

// List rig names (filenames without .json) found in `dir`.
std::vector<std::string> list(const std::string& dir);

// Load dir/<name>.json into the chain. false on read/parse error. The factory
// rebuilds the FX grid (which kind sits in which slot) recorded by the rig.
bool load(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl, FxFactory& factory);

// Snapshot the chain to dir/<name>.json. false on write error. Preserves the
// file's metadata identity (id/createdAt/provenance) across saves and refreshes
// updatedAt/contentHash; `idOut`, if given, receives the rig's stable id.
bool save(const std::string& dir, const std::string& name, Chain& chain,
          PedalControls& ctl, std::string* idOut = nullptr);

// Values-only document:
//   { name, master, bypassed,
//     fxGrid:[ null | {kind,id}, ... ],          // per-slot grid layout
//     effects:{ id:{enabled,fsAssign,fsMode,params} } }
nlohmann::json capture(const Chain& chain, const PedalControls& ctl);

// Apply a values-only document; missing keys leave current values untouched.
// If the document carries an fxGrid, the factory rebuilds the slots first so the
// effects map (keyed by type_id) resolves against the restored instances.
void apply(const nlohmann::json& doc, Chain& chain, PedalControls& ctl,
           FxFactory& factory);

}  // namespace rigs

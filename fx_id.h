// fx_id.h -- shared helper for mapping a chain instance id back to its kind.
//
// The factory mints unique type_ids per instance ("reverb", "reverb-2", ...).
// Rigs (whole-chain) and per-pedal presets both need to recover the kind from
// an id, so the rule lives here once.

#pragma once

#include <cctype>
#include <string>

// Strip a factory id's trailing instance number to recover its kind:
// "reverb" -> "reverb", "reverb-2" -> "reverb". (Factory ids never embed a
// trailing -<digits> in the kind itself, so this is unambiguous.)
inline std::string fxBaseKind(const std::string& id) {
  auto dash = id.rfind('-');
  if (dash == std::string::npos || dash + 1 >= id.size()) return id;
  const std::string tail = id.substr(dash + 1);
  for (char ch : tail)
    if (!std::isdigit((unsigned char)ch)) return id;
  return id.substr(0, dash);
}

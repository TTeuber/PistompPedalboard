// meta.h -- sync-ready metadata helpers shared by rigs/setlists/presets.
//
// Every saved item (rig, setlist, preset) carries a small metadata block so a
// future sync/sharing layer has a stable identity to key on: a UUID `id`,
// ISO-8601 `createdAt`/`updatedAt`, a `contentHash` for change detection, and
// reserved `origin`/`owner` provenance fields (written but unread for now).
// `stamp()` writes that block onto a document; `contentHash()` hashes ONLY the
// value subset (never the metadata), so re-saving unchanged content is a no-op
// to a differ. See ADR 0002.

#pragma once

#include <json.hpp>

#include <string>

namespace meta {

// A random UUID v4, formatted 8-4-4-4-12 lowercase hex.
std::string uuidv4();

// Current UTC time as "YYYY-MM-DDThh:mm:ssZ".
std::string nowIso8601();

// FNV-1a 64-bit hash of `content.dump()`, hex, prefixed "fnv1a64:". nlohmann's
// json sorts object keys, so the dump is deterministic without canonicalising.
// Portable + std-only on purpose (this is change detection, not security).
std::string contentHash(const nlohmann::json& content);

// Ensure `doc` carries the metadata block, hashing `content` (the value-only
// subset) for `contentHash`. Preserves an existing `id`/`createdAt`/`origin`/
// `owner` if present; always refreshes `updatedAt` + `contentHash`; seeds the
// reserved provenance fields to null on first write. `schema` versions the item.
void stamp(nlohmann::json& doc, const nlohmann::json& content, int schema);

}  // namespace meta

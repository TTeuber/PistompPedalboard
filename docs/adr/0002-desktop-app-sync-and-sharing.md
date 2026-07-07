# ADR 0002 — Desktop app, sync-ready data model, and rig sharing

- **Status:** Partially accepted — the **sync-ready data model** (Stage 1) is
  built (2026-06-29); the Tauri desktop app, LAN sync, and all of Stage 2 remain
  Proposed.
- **Date:** 2026-06-27 (data model implemented 2026-06-29)
- **Context area:** `pedalboard/` (engine + `webui/` + `web_server.cpp`), the
  Mac simulator, `pedalboard/{rigs,setlists,presets}/`

## Context

The pedalboard runs the same `pedalboard.cpp` engine on the v3 hardware and in
the Mac simulator (SDL display + miniaudio/CoreAudio). Control happens through a
Svelte UI (`webui/`) served by an embedded cpp-httplib + SSE server
(`web_server.cpp`), and rigs/setlists/presets are already small, portable JSON
files on disk.

Two desires motivate this ADR, neither of which is being built now:

1. **A standalone desktop app.** The owner wants to design rigs, build setlists,
   and practice on a computer without the hardware, then get that work onto the
   pedal. The simulator already proves the engine and UI run on a desktop — what
   is missing is packaging and a way to move data between desktop and device.
2. **Sharing between people.** The concrete use case is worship guitar: multiple
   guitarists at a church play the same songs and would rather not each rebuild
   the same tones, and teachers want to hand setlists/rigs to students. That
   turns a single-user *sync* problem into a multi-user *sharing* problem — a
   small content/collaboration platform — which is what justifies real accounts
   and a cloud backend.

The guiding constraint: **an account must never gate core function.** The app
should be fully usable offline, and two musicians on the same Wi-Fi should be
able to move rigs around without anyone signing up. Cloud is *additive reach*,
not a gate.

## Decision

Pursue this in two clearly separated stages, foundation first:

### Stage 1 — Foundation (build first)

- **Tauri desktop app.** Wrap the existing Svelte `webui/` in a Tauri shell and
  bundle the C++ engine (the Mac sim binary) as a **sidecar process**. The
  webview is the editor; the sidecar is the audio/DSP engine. This reuses the UI
  and engine wholesale rather than building a new app. Two runtime modes fall
  out of it: *practice* (local engine processes audio) and *remote* (UI points
  at a device's web server — essentially already works today).
- **Sync-ready data model.** ✅ **Built (2026-06-29).** Evolve each
  rig/setlist/preset to carry a stable **`id` (UUID)**,
  **`createdAt`/`updatedAt`**, an optional **`contentHash`**, and add a top-level
  **`manifest.json`** for the library (`{id, updatedAt, hash}` per item).
  Identity moves off the filename so renames stop looking like delete+create.
  This is the prerequisite for *any* sync or sharing.

  *As implemented:* a `meta` helper (`pedalboard/meta.{h,cpp}`) mints UUIDv4 ids,
  ISO-8601 UTC timestamps, and an FNV-1a-64 `contentHash` over the **value-only**
  subset (so re-saving identical content doesn't churn the hash, and the metadata
  block is excluded). A `manifest` module (`pedalboard/manifest.{h,cpp}`) owns the
  library index: a one-time, idempotent **startup migration** (`rebuild()`) stamps
  pre-existing files and rewrites legacy setlist references, plus incremental
  `upsertFile`/`removeFile` keep `manifest.json` live as items are saved/deleted.
  Each item also reserves null **`origin`/`owner`** provenance fields and a
  `schema` version so Stage 2 needs no further migration. Filenames stay
  human-readable; the `id` (not the filename) is the identity. **Setlists now
  reference rigs by `id`** (with a cached `name` label), and the server resolves
  refs through the manifest so a renamed rig still loads and a deleted one is
  flagged *missing* rather than silently breaking. `manifest.json` is a derived
  cache (gitignored, regenerated next to the binary at startup), so the on-disk
  JSON files remain the source of truth. Duplicate-name handling was explicitly
  **deferred** to Stage 2 (cloud import), per discussion.
- **LAN sync.** Add REST endpoints to the existing device web server
  (`GET /library/manifest`, `GET/PUT /rigs/:id`, etc.), discover devices via
  **mDNS/Bonjour** (`_pistomp._tcp`), and reconcile by manifest diff. No cloud,
  no account. This delivers "design on the desktop, push to the pedal on the same
  Wi-Fi" and "hand a rig to the guitarist next to you" on its own.

### Stage 2 — Cloud sharing (build later)

- **Supabase** as the sharing/social layer: accounts, **groups** (a worship team
  / a teacher's class) with memberships, share grants, and **Storage** for large
  binary dependencies (see below). Row-Level Security expresses the permission
  model directly.
- **Copy-first sharing.** Sharing a rig creates an independent copy in the
  recipient's library, stamped with **provenance** ("originally by X, adapted by
  Y"). A manual "check for updates from original" action may come later.

## Decisions settled in discussion (and why)

- **Copy, not subscribe, as the default share behavior.** Copy-and-adapt matches
  how peer guitarists actually work (each tunes for their own gear) and avoids
  owning a continuous-merge problem. Live "subscribe / auto-pull updates" is a
  later, optional layer — explicitly out of scope for the foundation.
- **"Song" stays an informal rig name, not a first-class entity.** This mirrors
  how the owner already organizes tones on their current multi-effects board. A
  rig named for its song (optionally searchable) is enough; we are not modeling
  a `song` table or song→rig relationships.
- **Supabase over Firebase.** The data is relational — users, rigs, setlists,
  groups, memberships, shares — and Supabase's Postgres + Row-Level Security map
  one-to-one onto "you can read a rig if it's yours, public, or shared with a
  group you belong to." Firestore is document/realtime-first; the relational
  permission queries get awkward and we don't need live multi-cursor editing.
  Supabase Storage also covers the asset problem below, and it is self-hostable.
- **Cloud is additive, never a gate.** Local + LAN is the baseline and works with
  no account. The account only unlocks what *inherently* needs a hub: sharing
  across people who aren't on the same network at the same time, backup, and
  discovery.

## Known hard parts (called out, not yet solved)

- **Rigs are not self-contained.** A chain can reference a **NAM amp capture or
  impulse response** — large binary assets the recipient may not have, and very
  common in worship guitar. Sharing therefore needs asset handling, not just
  JSON: either a **"rig package"** (zip of JSON + referenced assets) for
  LAN/file transfer, or **asset-by-content-hash** with fetch-on-demand from
  cloud Storage. This is a concrete reason Stage 2 needs Storage and a reason to
  settle the data model in Stage 1.
- **Graceful degradation.** When a dependency *or* an effect type referenced by a
  shared rig is missing on the loading device, the rig should still load, flag
  the missing piece, and offer to fetch or substitute — never silently break.
- **Conflicts.** If the same rig is edited on two devices between syncs, use
  **per-entity (not per-field) last-write-wins by `updatedAt`**, surfaced through
  a small "conflicts" review screen ("keep desktop / keep pedal / keep both").
  Deliberately *not* CRDTs — overkill for this scale.

## Why Tauri (over Electron, or shipping the simulator as-is)

- **Reuses what exists.** The Svelte UI already speaks HTTP/SSE to the C++
  engine; Tauri's webview-plus-sidecar pattern is built for exactly "ship a
  compiled binary alongside a web UI," so almost no new UI code is needed.
- **Over Electron:** far smaller binaries and a native webview, and we are
  already comfortable with native/Rust-adjacent tooling.
- **Over shipping the simulator `.app` directly:** the SDL display mimics the
  small hardware screen — fine as a stopgap, wrong for a "design on a big screen"
  experience. The Svelte UI is the real editor surface.

## Consequences

- **Stage 1 is independently valuable and unblocks Stage 2.** A real installable
  desktop app plus LAN sync is useful on its own, with no cloud commitment, and
  the data-model work it forces (IDs, timestamps, manifest, provenance fields) is
  exactly what cloud sharing later requires.
- **The data model should reserve provenance/ownership fields in Stage 1** even
  though nothing reads them yet, so Stage 2 doesn't require migrating existing
  libraries.
- **New surface area:** Tauri build/packaging per-platform, an engine sidecar,
  device-side sync endpoints + mDNS, and (Stage 2) a Supabase schema with RLS,
  auth, groups, and an asset/Storage story.
- **Out of scope (for now):** live subscribe/auto-pull sharing, a first-class
  `song` entity, multi-user realtime editing, and any cloud requirement for
  baseline use.

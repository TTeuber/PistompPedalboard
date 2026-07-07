# ADR 0001 — Svelte (+ SSE) for the pedalboard web UI, over React

- **Status:** Accepted
- **Date:** 2026-06-22
- **Context area:** `pedalboard/webui/`, `pedalboard/web_server.cpp`

## Context

The pedalboard has two control surfaces over one shared, in-memory state (the
`Chain` + `PedalControls` atomics): the on-device LCD/encoder UI and a browser
UI served by the embedded cpp-httplib server. Both read and write the same
atomics, so the device's state is already a single source of truth.

The browser UI began as ~230 lines of vanilla JS that fetched `/api/state` once
and rebuilt the whole DOM with `innerHTML = ""`. Two problems pushed us to change
it:

1. **No live sync.** A turn of a hardware encoder never reached the browser (it
   only fetched state on load), and a browser edit only reached the device
   because they share memory — not because anything was pushed.
2. **Drag-and-drop is coming.** A reorderable FX grid plus live updates means the
   UI now holds local interaction state that has to reconcile against incoming
   server state. Full-DOM rebuilds fight in-progress interactions (a live update
   yanks the slider you're dragging). That is reconciliation work — exactly what
   a view framework exists to do.

So we needed (a) a push channel for "state changed" and (b) a frontend whose
update model doesn't clobber active controls.

## Decision

**Frontend: Svelte 5 + TypeScript**, built by Vite into `pedalboard/web/` (served
as static files; no Node on the Pi). **Transport: Server-Sent Events**
(`GET /api/events`) for device→browser push, with the existing REST routes for
browser→device writes.

TypeScript earns its place because the entire UI is driven by one JSON contract
(`fullState()`): typing that shape (`webui/src/lib/types.ts`) catches the bugs
that are easy to introduce against it. Note Vite/esbuild only *strip* types, so
the build is `svelte-check && vite build` — `svelte-check` is what actually gates
on type errors.

## Why Svelte over React/Preact

- **Fine-grained reactivity fits the problem.** Our hardest UI requirement is
  "a value changed on the device — update *only that control*, without disturbing
  whatever the user is touching." Svelte compiles to surgical DOM updates, so a
  pushed value updates exactly one node. React/Preact solve the same thing by
  diffing a re-render — more machinery for the same outcome. The drag-guard in
  `ParamControl.svelte` (adopt server values unless `editing`) is small and
  natural because only that node is in play.
- **Footprint.** The whole app gzips to ~20 KB with no runtime framework shipped
  — appropriate for a device served off a Pi.
- **Less ceremony.** The reactive store (`store.svelte.js`) is a plain `$state`
  object that the SSE handler folds new state into; components read it directly.
- **Build, not runtime, cost.** Svelte's compile step is a non-issue: we already
  build in Docker, so Vite just emits static files Vite → `web/` → rsync.

### Considered and rejected

- **React / Preact.** The most résumé-recognizable option, and Preact can even go
  build-less. Rejected on technical fit: virtual-DOM diffing is the heavier path
  to the surgical updates we get natively from Svelte. (Acknowledged trade-off:
  React is more common in job listings; we chose the better fit for the device.)
- **SolidJS.** Arguably the *most* ideal here (fine-grained reactivity, tiny).
  Rejected only on familiarity — we have prior Svelte experience and none in
  Solid, and the reactivity benefit over Svelte is marginal for this app.
- **Keep vanilla JS + a revision counter and poll.** Adequate *today*, but the
  full-DOM-rebuild approach is the exact ceiling the drag grid would hit. We'd
  rather not migrate mid-feature.

## Why SSE over WebSockets (and over polling)

State changes are discrete, human-paced, and one client (occasionally two) on a
LAN. SSE is one-directional server→browser — precisely the "tell the browser
something changed" need — and is just a long-lived chunked HTTP response, far
simpler than a WebSocket. Writes stay on plain REST. WebSockets would buy
bidirectional streaming we don't need.

Implementation note: rather than hand-increment a revision at every mutation
site (easy to forget one), `/api/events` **diffs the serialized full state**
every ~100 ms and pushes only on change. The state blob is tiny, so this is
cheaper to reason about and impossible to desync. Telemetry (which changes every
audio block) is deliberately *not* in this stream — the browser polls it
separately so it doesn't trigger a re-render storm.

## Consequences

- **Build:** `docker/Dockerfile` gains Node (build-time only); a CMake target
  (`pedalboard_webui`) runs `npm ci && npm run build` incrementally, so
  `scripts/build.sh` builds web + C++ together. `pedalboard/web/` is now
  generated and gitignored; `webui/` (with a committed `package-lock.json`) is
  the source.
- **Backend:** added `GET /api/events` (SSE) and `POST /api/footswitch`; latched
  footswitch state moved from a private `UiController` member into shared
  `PedalControls` so the web and device agree on one truth.
- **httplib is thread-per-connection:** each open SSE stream parks one worker
  thread. Fine for one or two browsers; not a many-client design (which we don't
  need).
- **Next:** the drag-and-drop FX grid can now be built on a reconciling view
  layer with live push already in place.

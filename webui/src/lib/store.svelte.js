// store.svelte.js -- the single reactive source of truth for the whole UI.
//
// It's seeded once from /api/state, then kept LIVE by the /api/events SSE stream:
// the device pushes the full state whenever anything changes (an encoder turn, a
// footswitch tap, another browser's edit), and we fold it in here. Because these
// are Svelte 5 `$state` proxies, only the controls whose values actually changed
// re-render -- so a hardware change never yanks a slider you're dragging.

import { api } from './api.js';

export const board = $state({
  master: 1,
  bypassed: false,
  fxSlotCount: 8,
  footswitches: [true, true, true, true], // FS1..FS4 latched engaged-state
  effects: [],
  fxKinds: [],
});

export const status = $state({ dspPermille: 0, xruns: 0, live: false });

// Fold a full-state document into the store, field by field (so the proxy tracks
// each change and any future server-added keys are harmlessly ignored).
export function applyState(s) {
  if (!s) return;
  if ('master' in s) board.master = s.master;
  if ('bypassed' in s) board.bypassed = s.bypassed;
  if ('fxSlotCount' in s) board.fxSlotCount = s.fxSlotCount;
  if ('footswitches' in s) board.footswitches = s.footswitches;
  if ('effects' in s) board.effects = s.effects;
  if ('fxKinds' in s) board.fxKinds = s.fxKinds;
}

// Open the live stream. EventSource auto-reconnects on a dropped connection; we
// just flip `live` so the header can show the link state.
export function connectLive() {
  const es = new EventSource('/api/events');
  es.onmessage = (e) => {
    try {
      applyState(JSON.parse(e.data));
      status.live = true;
    } catch (_) {
      /* ignore a malformed frame */
    }
  };
  es.onerror = () => {
    status.live = false;
  };
  return es;
}

export async function refresh() {
  applyState(await api('/api/state'));
}

// Telemetry changes every audio block, so it is deliberately OUT of the SSE state
// stream (it would cause a re-render storm); the UI polls it on a slow timer.
export async function pollTelemetry() {
  try {
    const t = await api('/api/telemetry');
    status.dspPermille = t.dspPermille;
    status.xruns = t.xruns;
  } catch (_) {
    /* transient -- try again next tick */
  }
}

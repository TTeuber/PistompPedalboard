// store.svelte.ts -- the single reactive source of truth for the whole UI.
//
// It's seeded once from /api/state, then kept LIVE by the /api/events SSE stream:
// the device pushes the full state whenever anything changes (an encoder turn, a
// footswitch tap, another browser's edit), and we fold it in here. Because these
// are Svelte 5 `$state` proxies, only the controls whose values actually changed
// re-render -- so a hardware change never yanks a slider you're dragging.

import { api } from './api.js';
import type { BoardState, Telemetry } from './types.js';

export const board: BoardState = $state({
  master: 1,
  bypassed: false,
  fxSlotCount: 8,
  footswitches: [true, true, true, true], // FS1..FS4 latched engaged-state
  effects: [],
  fxKinds: [],
});

export const status = $state({ dspPermille: 0, xruns: 0, live: false });

// Fold a (possibly partial) full-state document into the store, field by field,
// so the proxy tracks each change and any future server-added keys are ignored.
export function applyState(s: Partial<BoardState> | null): void {
  if (!s) return;
  if (s.master !== undefined) board.master = s.master;
  if (s.bypassed !== undefined) board.bypassed = s.bypassed;
  if (s.fxSlotCount !== undefined) board.fxSlotCount = s.fxSlotCount;
  if (s.footswitches !== undefined) board.footswitches = s.footswitches;
  if (s.effects !== undefined) board.effects = s.effects;
  if (s.fxKinds !== undefined) board.fxKinds = s.fxKinds;
}

// Open the live stream. EventSource auto-reconnects on a dropped connection; we
// just flip `live` so the header can show the link state.
export function connectLive(): EventSource {
  const es = new EventSource('/api/events');
  es.onmessage = (e: MessageEvent<string>) => {
    try {
      applyState(JSON.parse(e.data) as BoardState);
      status.live = true;
    } catch {
      /* ignore a malformed frame */
    }
  };
  es.onerror = () => {
    status.live = false;
  };
  return es;
}

export async function refresh(): Promise<void> {
  applyState(await api<BoardState>('/api/state'));
}

// Telemetry changes every audio block, so it is deliberately OUT of the SSE state
// stream (it would cause a re-render storm); the UI polls it on a slow timer.
export async function pollTelemetry(): Promise<void> {
  try {
    const t = await api<Telemetry>('/api/telemetry');
    status.dspPermille = t.dspPermille;
    status.xruns = t.xruns;
  } catch {
    /* transient -- try again next tick */
  }
}

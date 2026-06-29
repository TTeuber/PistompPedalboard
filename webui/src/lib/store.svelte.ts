// store.svelte.ts -- the single reactive source of truth for the whole UI.
//
// It's seeded once from /api/state, then kept LIVE by the /api/events SSE stream:
// the device pushes the full state whenever anything changes (an encoder turn, a
// footswitch tap, another browser's edit), and we fold it in here. Because these
// are Svelte 5 `$state` proxies, only the controls whose values actually changed
// re-render -- so a hardware change never yanks a slider you're dragging.

import { api } from './api.js';
import type { BoardState, Meters, PedalPresetList, Telemetry } from './types.js';

export const board: BoardState = $state({
  master: 1,
  bypassed: false,
  fxSlotCount: 8,
  footswitches: [true, true, true, true], // FS1..FS4 latched engaged-state
  effects: [],
  fxKinds: [],
});

export const status = $state({ dspPermille: 0, xruns: 0, live: false });

// Live audio levels for the input/output meters. Like telemetry, kept OUT of the
// SSE state stream (it changes every audio block) and polled on a fast timer; the
// device clears its peak holds on each read, so these fall back between polls.
export const meters = $state<Meters>({ inputDb: -60, outputDbL: -60, outputDbR: -60, grDb: 0 });

// Per-pedal preset NAMES, cached by pedal KIND ("drive", "reverb", ...). Every
// instance of a kind shares one list; we (re)fetch lazily and after edits.
export const pedalPresets: Record<string, string[]> = $state({});

// Mirror of the server's fxBaseKind(): "drive-2" -> "drive", "drive" -> "drive".
export function fxKind(type: string): string {
  const m = type.match(/^(.*)-\d+$/);
  return m ? m[1] : type;
}

// Fetch (and cache) the preset names for the kind of `effectType`.
export async function loadPedalPresetNames(effectType: string): Promise<void> {
  try {
    const r = await api<PedalPresetList>(
      `/api/pedal-presets?effect=${encodeURIComponent(effectType)}`,
    );
    pedalPresets[r.kind] = r.names;
  } catch {
    /* ignore -- the dropdown just stays empty */
  }
}

// Fetch (and cache) preset names for a base KIND directly -- used by the
// add-effect picker, which needs a kind's presets before any instance is placed.
export async function loadPedalPresetNamesByKind(kind: string): Promise<string[]> {
  try {
    const r = await api<PedalPresetList>(`/api/pedal-presets?kind=${encodeURIComponent(kind)}`);
    pedalPresets[r.kind] = r.names;
    return r.names;
  } catch {
    return [];
  }
}

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

// Levels poll faster than telemetry (the meters need to feel live). Same "out of
// SSE, into a timer" rationale; each GET reads-and-clears the device peak holds.
export async function pollMeters(): Promise<void> {
  try {
    const m = await api<Meters>('/api/meters');
    meters.inputDb = m.inputDb;
    meters.outputDbL = m.outputDbL;
    meters.outputDbR = m.outputDbR;
    meters.grDb = m.grDb;
  } catch {
    /* transient -- try again next tick */
  }
}

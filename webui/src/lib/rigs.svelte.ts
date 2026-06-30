// rigs.svelte.ts -- shared rig/setlist browsing state.
//
// Both the left sidebar and the top-bar rig controls drive the same thing:
// which setlist is selected, which rigs it shows, and which rig is loaded onto
// the board. Keeping that here (instead of inside one component) lets the
// header's Save/New/Prev/Next buttons and the sidebar stay in lockstep -- step
// a rig from the top bar and the sidebar highlight moves with it.

import { api } from './api.js';
import { applyState, board } from './store.svelte.js';
import { clearActivePresets } from './presets.svelte.js';
import type { BoardState, RigRef, Setlist } from './types.js';

export const ALL = '__all__'; // pseudo-setlist: the whole rig catalogue

export const rigState = $state({
  setlistNames: [] as string[],
  allRigs: [] as string[],
  selSetlist: ALL,
  viewRigs: [] as RigRef[], // rigs shown for the current selection
  activeRig: '', // last rig we loaded (for highlight + stepping)
  baseSig: '', // board signature as of the last load/save (for the dirty flag)
});

// A canonical snapshot of exactly what a rig persists (master/bypass + each
// effect's enabled/footswitch/params + grid order). We diff the live board
// against this to know if the loaded rig has unsaved edits.
function rigSignature(): string {
  return JSON.stringify({
    master: board.master,
    bypassed: board.bypassed,
    fx: board.effects.map((e) => ({
      t: e.type,
      en: e.enabled,
      sl: e.slot,
      fa: e.fsAssign,
      fm: e.fsMode,
      p: e.params.map((p) => [p.id, p.value]),
    })),
  });
}
// Mark the live board as matching the loaded rig (called on load and save).
export function markRigClean(): void {
  rigState.baseSig = rigSignature();
}
// True when a rig is loaded and the board has diverged from its saved state.
// Reactive: reads the board $state, so it recomputes as knobs/effects change.
export function rigDirty(): boolean {
  return !!rigState.activeRig && rigSignature() !== rigState.baseSig;
}

// (Re)load the catalogue + setlist names, preserving the current selection.
export async function loadLists(): Promise<void> {
  try {
    const [names, rigs] = await Promise.all([
      api<string[]>('/api/setlists'),
      api<string[]>('/api/rigs'),
    ]);
    rigState.setlistNames = names;
    rigState.allRigs = rigs;
  } catch {
    /* ignore -- keep whatever we had */
  }
  await chooseSetlist(rigState.selSetlist);
}

export async function chooseSetlist(name: string): Promise<void> {
  rigState.selSetlist = name;
  if (name === ALL) {
    // The whole catalogue is just names; present them as refs (always present).
    rigState.viewRigs = rigState.allRigs.map((n) => ({ id: '', name: n }));
    return;
  }
  try {
    const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(name)}`);
    rigState.viewRigs = s.rigs;
  } catch {
    rigState.viewRigs = [];
  }
}

export async function pickRig(name: string): Promise<void> {
  if (!name) return;
  rigState.activeRig = name;
  applyState(await api<BoardState>('/api/rig/load', { name }));
  clearActivePresets(); // the rig (re)defines the board; preset memory is stale
  markRigClean(); // the board now matches the rig we just loaded
}

// Move to the prev/next loadable rig in the current view, wrapping around the
// ends. From nothing, jump to an end. Missing rigs (deleted from the library)
// are skipped over; if every rig is missing we stay put.
export function step(dir: number): void {
  const rigs = rigState.viewRigs;
  const n = rigs.length;
  if (!n) return;
  const cur = rigs.findIndex((r) => r.name === rigState.activeRig);
  let next = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + dir + n) % n;
  for (let i = 0; i < n && rigs[next].missing; i++) next = (next + dir + n) % n;
  if (rigs[next].missing) return; // every rig in the view is missing
  pickRig(rigs[next].name);
}

// Snapshot the live board under `name`. A new name joins the catalogue and
// becomes the active rig; an existing name overwrites in place.
export async function saveRig(name: string): Promise<void> {
  const n = name.trim();
  if (!n) return;
  await api('/api/rig/save', { name: n });
  rigState.activeRig = n;
  markRigClean(); // saved -> the board is the clean baseline again
  await loadLists();
}

// Rename the loaded rig (preserves its id, so setlists keep resolving).
export async function renameRig(from: string, to: string): Promise<void> {
  const t = to.trim();
  if (!t || t === from) return;
  await api('/api/rig/rename', { from, to: t });
  rigState.activeRig = t;
  await loadLists();
}

// Delete a rig from the library. The live board keeps playing; it just stops
// being a named rig (the asterisk/title clear).
export async function deleteRig(name: string): Promise<void> {
  if (!name) return;
  await api('/api/rig/delete', { name });
  if (rigState.activeRig === name) rigState.activeRig = '';
  await loadLists();
}

// rigs.svelte.ts -- shared rig/setlist browsing state.
//
// Both the left sidebar and the top-bar rig controls drive the same thing:
// which setlist is selected, which rigs it shows, and which rig is loaded onto
// the board. Keeping that here (instead of inside one component) lets the
// header's Save/New/Prev/Next buttons and the sidebar stay in lockstep -- step
// a rig from the top bar and the sidebar highlight moves with it.

import { api } from './api.js';
import { applyState } from './store.svelte.js';
import type { BoardState, Setlist } from './types.js';

export const ALL = '__all__'; // pseudo-setlist: the whole rig catalogue

export const rigState = $state({
  setlistNames: [] as string[],
  allRigs: [] as string[],
  selSetlist: ALL,
  viewRigs: [] as string[], // rigs shown for the current selection
  activeRig: '', // last rig we loaded (for highlight + stepping)
});

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
    rigState.viewRigs = rigState.allRigs;
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
  rigState.activeRig = name;
  applyState(await api<BoardState>('/api/rig/load', { name }));
}

// Move to the prev/next rig in the current view; from nothing, jump to an end.
export function step(dir: number): void {
  const rigs = rigState.viewRigs;
  if (!rigs.length) return;
  const cur = rigs.indexOf(rigState.activeRig);
  const next =
    cur < 0
      ? dir > 0
        ? 0
        : rigs.length - 1
      : Math.min(rigs.length - 1, Math.max(0, cur + dir));
  pickRig(rigs[next]);
}

// Snapshot the live board under `name`. A new name joins the catalogue and
// becomes the active rig; an existing name overwrites in place.
export async function saveRig(name: string): Promise<void> {
  const n = name.trim();
  if (!n) return;
  await api('/api/rig/save', { name: n });
  rigState.activeRig = n;
  await loadLists();
}

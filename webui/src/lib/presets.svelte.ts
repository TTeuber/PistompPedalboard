// presets.svelte.ts -- shared per-pedal preset state.
//
// Mirrors the rig "dirty" model (rigs.svelte.ts) for per-pedal presets: it
// remembers which preset is loaded on each effect INSTANCE (keyed by type_id)
// and a signature of that instance's param values at load/save time, so the FX
// panel can flag unsaved knob edits with a "*". The actual preset files are
// scoped by kind, but a loaded preset is an instance fact (drive-1 and drive-2
// can hold different presets), so we track it per type_id.

import { api } from './api.js';
import { applyState, board, loadPedalPresetNames } from './store.svelte.js';
import type { BoardState } from './types.js';

// type_id -> { name of the loaded preset, param signature when it was clean }.
const active = $state<Record<string, { name: string; sig: string }>>({});

// Canonical snapshot of an instance's param values (what a preset persists).
function paramSig(type: string): string {
  const fx = board.effects.find((e) => e.type === type);
  return fx ? JSON.stringify(fx.params.map((p) => [p.id, p.value])) : '';
}

// The preset currently loaded on this instance ('' = none).
export function activePresetName(type: string): string {
  return active[type]?.name ?? '';
}
// Mark the instance's knobs as matching the loaded preset (load / save).
export function markPresetClean(type: string, name: string): void {
  active[type] = { name, sig: paramSig(type) };
}
// True when a preset is loaded and the knobs have diverged from it. Reactive:
// reads the board $state so it recomputes as params change.
export function presetDirty(type: string): boolean {
  const a = active[type];
  return !!a && paramSig(type) !== a.sig;
}

// Loading a preset re-voices just this pedal; the server returns full state so
// the changed knobs fold straight back in.
export async function loadPresetFor(type: string, name: string): Promise<void> {
  if (!name) return;
  applyState(await api<BoardState>('/api/pedal-preset/load', { effect: type, name }));
  markPresetClean(type, name);
}
// Save the live knobs under `name` (new or overwrite) and make it the clean base.
export async function savePresetAs(type: string, name: string): Promise<void> {
  const n = name.trim();
  if (!n) return;
  await api('/api/pedal-preset/save', { effect: type, name: n });
  await loadPedalPresetNames(type);
  markPresetClean(type, n);
}
// Rename a preset (id-preserving server-side); keep the active mapping in sync.
export async function renamePresetFor(type: string, from: string, to: string): Promise<void> {
  const t = to.trim();
  if (!t || t === from) return;
  await api('/api/pedal-preset/rename', { effect: type, from, to: t });
  await loadPedalPresetNames(type);
  if (active[type]?.name === from) active[type] = { name: t, sig: active[type].sig };
}
// Delete a preset; the knobs keep their values, they're just no longer "a preset".
export async function deletePresetFor(type: string, name: string): Promise<void> {
  if (!name) return;
  await api('/api/pedal-preset/delete', { effect: type, name });
  await loadPedalPresetNames(type);
  if (active[type]?.name === name) delete active[type];
}

// Loading a whole rig redefines the board, so any "loaded preset" memory is now
// stale -- clear it (rigs.svelte.ts calls this on rig load).
export function clearActivePresets(): void {
  for (const k in active) delete active[k];
}

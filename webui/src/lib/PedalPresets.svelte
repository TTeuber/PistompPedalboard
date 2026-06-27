<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import { applyState, pedalPresets, loadPedalPresetNames, fxKind } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';

  let { fx }: { fx: Effect } = $props();

  // Per-pedal presets are scoped by KIND, so every Drive shares one list.
  const kind = $derived(fxKind(fx.type));
  const presetNames = $derived(pedalPresets[kind] ?? []);
  let presetSel = $state('');
  let saveName = $state('');

  onMount(() => {
    loadPedalPresetNames(fx.type);
  });

  // Loading a preset re-voices just this pedal; the server returns full state so
  // the changed knobs fold straight back in.
  async function loadPreset() {
    if (!presetSel) return;
    applyState(await api<BoardState>('/api/pedal-preset/load', { effect: fx.type, name: presetSel }));
  }
  async function savePreset() {
    const name = saveName.trim();
    if (!name) return;
    await api('/api/pedal-preset/save', { effect: fx.type, name });
    saveName = '';
    await loadPedalPresetNames(fx.type);
    presetSel = name;
  }
  async function deletePreset() {
    if (!presetSel) return;
    await api('/api/pedal-preset/delete', { effect: fx.type, name: presetSel });
    presetSel = '';
    await loadPedalPresetNames(fx.type);
  }
</script>

<div class="pedal-presets">
  <select bind:value={presetSel} aria-label="Preset for {fx.name}">
    <option value="">preset…</option>
    {#each presetNames as n}
      <option value={n}>{n}</option>
    {/each}
  </select>
  <button class="mini" title="Load preset" disabled={!presetSel} onclick={loadPreset}>Load</button>
  <button class="mini danger" title="Delete preset" disabled={!presetSel} onclick={deletePreset}>✕</button>
</div>
<div class="pedal-presets">
  <input type="text" placeholder="save as…" bind:value={saveName} />
  <button class="mini" title="Save current knobs as a preset" onclick={savePreset}>Save</button>
</div>

<style>
  /* Per-pedal preset rows: pick/save a knob snapshot for the kind. */
  .pedal-presets { display: flex; gap: var(--sp-2); margin-bottom: var(--sp-3); }
  .pedal-presets select,
  .pedal-presets input {
    flex: 1;
    min-width: 0;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-size: var(--fs-xs);
  }
  .pedal-presets :global(.mini) { flex: 0 0 auto; padding: var(--sp-2) var(--sp-3); }
</style>

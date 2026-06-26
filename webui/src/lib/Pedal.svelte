<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import {
    applyState,
    pedalPresets,
    loadPedalPresetNames,
    fxKind,
  } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import AssignStrip from './AssignStrip.svelte';

  let { fx, inGrid = false }: { fx: Effect; inGrid?: boolean } = $props();

  // Per-pedal presets are scoped by KIND, so every Drive shares one list.
  const kind = $derived(fxKind(fx.type));
  const presetNames = $derived(pedalPresets[kind] ?? []);
  let presetSel = $state('');
  let saveName = $state('');

  onMount(() => {
    loadPedalPresetNames(fx.type);
  });

  function togglePower() {
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }

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

  // Grid moves/removes return the full new state (the chain re-laid out), so we
  // fold the response straight into the store.
  async function move(dir: number) {
    applyState(await api<BoardState>('/api/fx/move', { slot: fx.slot, dir }));
  }
  async function remove() {
    applyState(await api<BoardState>('/api/fx/remove', { slot: fx.slot }));
  }
</script>

<div class="pedal" class:off={!fx.enabled}>
  <div class="pedal-head">
    <h3>{fx.name}</h3>
    <button
      class="power"
      class:on={fx.enabled}
      title="Enable / disable"
      aria-label="Enable / disable {fx.name}"
      onclick={togglePower}
    ></button>
  </div>

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

  {#each fx.params as p (p.id)}
    <ParamControl effectType={fx.type} {p} />
  {/each}

  {#if inGrid}
    <AssignStrip {fx} />
    <div class="pedal-foot">
      <button class="mini" title="Move toward input" onclick={() => move(-1)}>◀</button>
      <button class="mini danger" title="Remove" onclick={remove}>✕</button>
      <button class="mini" title="Move toward output" onclick={() => move(1)}>▶</button>
    </div>
  {/if}
</div>

<style>
  .pedal {
    background: var(--panel-2);
    border: 1px solid var(--line);
    border-radius: var(--r-lg);
    padding: var(--sp-4);
    transition: opacity var(--t-med), border-color var(--t-med);
  }
  .pedal.off { opacity: .5; }
  .pedal-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: var(--sp-3); }
  .pedal-head h3 {
    font-size: var(--fs-sm);
    margin: 0;
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--accent);
  }

  /* On/off power dot. */
  .power {
    width: 26px;
    height: 26px;
    border-radius: 50%;
    border: 2px solid var(--line);
    background: var(--inset);
    cursor: pointer;
  }
  .power.on { border-color: var(--ok); box-shadow: var(--glow) var(--ok); background: var(--ok); }

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

  .pedal-foot { display: flex; gap: var(--sp-2); }
</style>
